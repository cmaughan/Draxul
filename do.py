#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import pathlib
import re
import shlex
import shutil
import stat
import subprocess
import sys
from datetime import datetime
from types import SimpleNamespace


REVIEW_MODES = {
    "all": "All",
    "codex": "Codex",
    "gpt": "Codex",
    "agy": "Agy",
    "gemini": "Agy",
    "claude": "Claude",
}

CODEX_REVIEW_MODEL = "gpt-5.6-sol"
CLAUDE_REVIEW_MODEL = "fable"

REVIEW_FAILURE_PATTERNS = (
    "CreateProcessAsUserW failed: 1312",
    "Agy produced no stdout",
    "You are not logged into Antigravity",
    "failed to get model config",
    "failed to get load code assist response",
    "Failed to get OAuth token",
    "permission denied",
    "not recognized as",
)

WINDOWS_CRT_RUNTIME_LIBRARIES = (
    "msvcp140.dll",
    "msvcp140_atomic_wait.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent


def build_dir(root: pathlib.Path) -> pathlib.Path:
    return root / "build"


def draxul_exe(bd: pathlib.Path, config: str) -> pathlib.Path:
    """Return the expected executable path for a given build dir and config."""
    if sys.platform.startswith("win"):
        config_exe = bd / config / "draxul.exe"
        if config_exe.exists():
            return config_exe
        return bd / "draxul.exe"
    bundle_exe = bd / "draxul.app" / "Contents" / "MacOS" / "draxul"
    if bundle_exe.exists():
        return bundle_exe
    return bd / "draxul"


def draxul_path(root: pathlib.Path) -> pathlib.Path:
    """Legacy helper — probe common locations for the executable."""
    if sys.platform.startswith("win"):
        release = build_dir(root) / "Release" / "draxul.exe"
        if release.exists():
            return release
        debug = build_dir(root) / "Debug" / "draxul.exe"
        if debug.exists():
            return debug
        return release
    bundle_exe = build_dir(root) / "draxul.app" / "Contents" / "MacOS" / "draxul"
    if bundle_exe.exists():
        return bundle_exe
    return build_dir(root) / "draxul"


# ---------------------------------------------------------------------------
# Build helpers for the `run` command
# ---------------------------------------------------------------------------

_VSDEVCMD_SEARCH_PATHS = [
    r"C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\Tools\VsDevCmd.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
    r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Preview\Common7\Tools\VsDevCmd.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
]


def _capture_msvc_env(bat_path: str, bat_args: list[str]) -> dict[str, str] | None:
    """Run a VS env-setup .bat file and capture the resulting environment.

    Uses a temporary batch file to avoid quoting issues when subprocess
    launches cmd.exe from Git Bash or other non-cmd shells.
    """
    import tempfile

    args_str = " ".join(bat_args)
    bat_content = f'@call "{bat_path}" {args_str} >nul 2>&1\r\nset\r\n'
    tmp_bat = os.path.join(tempfile.gettempdir(), "_draxul_env.bat")
    try:
        with open(tmp_bat, "wb") as f:
            f.write(bat_content.encode("ascii"))
        result = subprocess.run(
            ["cmd", "/c", tmp_bat],
            capture_output=True, text=True, check=False,
            encoding="utf-8", errors="replace",
        )
    finally:
        if os.path.isfile(tmp_bat):
            os.unlink(tmp_bat)

    if result.returncode != 0:
        return None
    env: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            env[k] = v
    if not env:
        return None
    # Verify that cl.exe is actually on the resulting PATH.
    path_val = env.get("Path", env.get("PATH", ""))
    for d in path_val.split(";"):
        if os.path.isfile(os.path.join(d, "cl.exe")):
            return env
    return None


def _ensure_msvc_env() -> dict[str, str]:
    """If `cl.exe` is not on PATH, find VsDevCmd.bat and capture its env."""
    if shutil.which("cl"):
        return dict(os.environ)

    for p in _VSDEVCMD_SEARCH_PATHS:
        if not os.path.isfile(p):
            continue
        env = _capture_msvc_env(p, ["-arch=x64", "-host_arch=x64"])
        if env:
            return env

    # Also try vcvarsall.bat (more reliable when vswhere is missing).
    for p in _VSDEVCMD_SEARCH_PATHS:
        vcvars = pathlib.Path(p).parents[2] / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
        if not vcvars.is_file():
            continue
        env = _capture_msvc_env(str(vcvars), ["x64"])
        if env:
            return env

    print("\nFailed to initialise the MSVC toolchain for Ninja builds.")
    print("Use --vs to fall back to the Visual Studio generator.")
    sys.exit(1)


def _cache_build_type(cache_file: pathlib.Path) -> str | None:
    """Read CMAKE_BUILD_TYPE from an existing CMakeCache.txt."""
    if not cache_file.exists():
        return None
    for line in cache_file.read_text().splitlines():
        if line.startswith("CMAKE_BUILD_TYPE:STRING="):
            return line.split("=", 1)[1]
    return None


def _cache_value(cache_file: pathlib.Path, key: str) -> str | None:
    """Read a typed CMake cache value by name."""
    if not cache_file.exists():
        return None
    prefix = f"{key}:"
    for line in cache_file.read_text().splitlines():
        if line.startswith(prefix):
            return line.split("=", 1)[1]
    return None


def _missing_generated_build_file(
    cache_file: pathlib.Path, bd: pathlib.Path, config: str,
) -> pathlib.Path | None:
    """Return a missing generated build-system file, if the cache is incomplete."""
    generator = _cache_value(cache_file, "CMAKE_GENERATOR")
    if generator is None:
        return None

    if generator == "Ninja Multi-Config":
        build_file = bd / "build.ninja"
        if not build_file.exists():
            return build_file
        config_build_file = bd / f"build-{config}.ninja"
        if not config_build_file.exists():
            return config_build_file
        return None

    if generator == "Ninja":
        build_file = bd / "build.ninja"
        return None if build_file.exists() else build_file

    if generator.startswith("Visual Studio"):
        solution_file = bd / "draxul.sln"
        return None if solution_file.exists() else solution_file

    if generator.endswith("Makefiles"):
        makefile = bd / "Makefile"
        return None if makefile.exists() else makefile

    return None


def _check_metal_toolchain() -> None:
    if sys.platform != "darwin":
        return
    if shutil.which("xcrun") is None:
        print("Missing xcrun. Install Xcode Command Line Tools.", file=sys.stderr)
        sys.exit(1)
    r = subprocess.run(["xcrun", "--find", "metal"], capture_output=True, check=False)
    if r.returncode != 0:
        print("Missing Metal compiler. Install Xcode Command Line Tools and the Metal toolchain.", file=sys.stderr)
        print("Suggested fix: xcodebuild -downloadComponent MetalToolchain", file=sys.stderr)
        sys.exit(1)
    r = subprocess.run(["xcrun", "-sdk", "macosx", "metal", "-v"], capture_output=True, check=False)
    if r.returncode != 0:
        print("The Metal compiler is present but not runnable because the Metal Toolchain is missing.", file=sys.stderr)
        print("Suggested fix: xcodebuild -downloadComponent MetalToolchain", file=sys.stderr)
        sys.exit(1)


def _parse_build_args(args: list[str]) -> tuple[str, bool, str, bool, list[str]]:
    """Parse shared build/run arguments.

    Returns (mode, force_reconfigure, build_system, use_console, app_args).
    """
    mode = "debug"
    force_reconfigure = False
    build_system = "ninja"
    use_console = False
    app_args: list[str] = []

    i = 0
    while i < len(args):
        a = args[i]
        mode_arg = a.lower()
        if mode_arg in ("debug", "release", "relwithdebinfo"):
            mode = mode_arg
        elif a == "--reconfigure":
            force_reconfigure = True
        elif a == "--vs":
            build_system = "vs"
        elif a == "--ninja":
            build_system = "ninja"
        elif a == "--console":
            use_console = True
            app_args.append(a)
        elif a == "--":
            app_args.extend(args[i + 1:])
            break
        else:
            app_args.append(a)
        i += 1

    return mode, force_reconfigure, build_system, use_console, app_args


def _normalize_megacity_parser(parser: str) -> str:
    parser = parser.lower().replace("-", "_")
    if parser in ("treesitter", "tree_sitter", "treesitter_db", "tree_sitter_db"):
        return "treesitter_db"
    raise ValueError("--parser must be one of: treesitter, treesitter_db")


def _has_megacity_host(app_args: list[str]) -> bool:
    for i, arg in enumerate(app_args):
        if arg == "--host" and i + 1 < len(app_args):
            return app_args[i + 1].lower() == "megacity"
    return False


def _consume_megacity_parser_args(app_args: list[str]) -> tuple[list[str], str | None]:
    """Consume do.py's MegaCity parser helper flag from app args."""
    parser: str | None = None
    stripped_args: list[str] = []
    i = 0
    while i < len(app_args):
        arg = app_args[i]
        if arg == "--parser":
            if i + 1 >= len(app_args):
                raise ValueError("--parser requires a value")
            if parser is not None:
                raise ValueError("--parser may be specified only once")
            parser = _normalize_megacity_parser(app_args[i + 1])
            i += 2
            continue
        stripped_args.append(arg)
        i += 1

    if parser is not None and not _has_megacity_host(stripped_args):
        raise ValueError("--parser is only supported with --host megacity")
    return stripped_args, parser


def _default_config_path() -> pathlib.Path:
    if sys.platform.startswith("win"):
        base = pathlib.Path(os.environ.get("APPDATA") or ".")
        return base / "draxul" / "config.toml"
    if sys.platform == "darwin":
        base = pathlib.Path(os.environ.get("HOME") or ".")
        return base / "Library" / "Application Support" / "draxul" / "config.toml"
    xdg = os.environ.get("XDG_CONFIG_HOME")
    if xdg:
        base = pathlib.Path(xdg)
    else:
        base = pathlib.Path(os.environ.get("HOME") or ".") / ".config"
    return base / "draxul" / "config.toml"


def _toml_string(value: str) -> str:
    return json.dumps(value)


def _merge_key_value(lines: list[str], start: int, end: int, key: str, value: str, newline: str) -> None:
    pattern = re.compile(rf"^(\s*){re.escape(key)}\s*=")
    replacement = f"{key} = {_toml_string(value)}{newline}"
    for index in range(start, end):
        match = pattern.match(lines[index])
        if match:
            indent = match.group(1)
            lines[index] = f"{indent}{replacement}"
            return
    insert_at = end
    while insert_at > start and lines[insert_at - 1].strip() == "":
        insert_at -= 1
    lines.insert(insert_at, replacement)


def _table_end(lines: list[str], section_start: int) -> int:
    for index in range(section_start + 1, len(lines)):
        stripped = lines[index].strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            return index
    return len(lines)


def _remove_key_value(lines: list[str], start: int, end: int, key: str) -> int:
    pattern = re.compile(rf"^\s*{re.escape(key)}\s*=")
    index = start
    while index < end:
        if pattern.match(lines[index]):
            del lines[index]
            end -= 1
            continue
        index += 1
    return end


def _merge_megacity_parser_config(text: str, parser: str) -> str:
    parser = _normalize_megacity_parser(parser)
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines(keepends=True)
    section_start: int | None = None
    section_end = len(lines)

    for index, line in enumerate(lines):
        if line.strip() == "[mega_city_code]":
            section_start = index
            break

    if section_start is None:
        prefix = "" if not text else newline
        if text and not text.endswith(("\n", "\r")):
            prefix = newline + prefix
        merged = f"{text}{prefix}[mega_city_code]{newline}"
        merged += f"code_source = {_toml_string(parser)}{newline}"
        return merged

    section_end = _table_end(lines, section_start)

    _merge_key_value(lines, section_start + 1, section_end, "code_source", parser, newline)
    section_end = _table_end(lines, section_start)
    _remove_key_value(lines, section_start + 1, section_end, "graphify_graph_path")
    return "".join(lines)


def _apply_megacity_parser_config(parser: str) -> pathlib.Path:
    config_path = _default_config_path()
    text = config_path.read_text(encoding="utf-8") if config_path.exists() else ""
    merged = _merge_megacity_parser_config(text, parser)
    config_path.parent.mkdir(parents=True, exist_ok=True)
    config_path.write_text(merged, encoding="utf-8")
    return config_path


def _configure_and_build(
    root: pathlib.Path, mode: str, force_reconfigure: bool, build_system: str,
) -> tuple[int, pathlib.Path, str, dict[str, str] | None]:
    """Configure + build.  Returns (rc, build_dir, config, env)."""
    is_win = sys.platform.startswith("win")
    is_mac = sys.platform.startswith("darwin")

    if is_win:
        if build_system == "ninja":
            config = {
                "debug": "Debug",
                "release": "Release",
                "relwithdebinfo": "RelWithDebInfo",
            }[mode]
            preset = f"win-ninja-{mode}"
            bd = root / f"build-ninja-{mode}"
        else:
            config = {
                "debug": "Debug",
                "release": "Release",
                "relwithdebinfo": "RelWithDebInfo",
            }[mode]
            preset = "default" if mode == "debug" else "release"
            bd = root / "build"
    elif is_mac:
        if mode == "relwithdebinfo":
            print("RelWithDebInfo is currently supported only on Windows in do.py. Use raw cmake if you need it on macOS.", file=sys.stderr)
            return 1, root / "build", "RelWithDebInfo", None
        config = "Debug" if mode == "debug" else "Release"
        preset = f"mac-{mode}"
        bd = root / "build"
    else:
        if mode == "relwithdebinfo":
            print("RelWithDebInfo is currently supported only on Windows in do.py. Use raw cmake if you need it on this platform.", file=sys.stderr)
            return 1, root / "build", "RelWithDebInfo", None
        config = "Debug" if mode == "debug" else "Release"
        preset = f"mac-{mode}"
        bd = root / "build"

    cache_file = bd / "CMakeCache.txt"

    print(f"\n=== {config} / {build_system if is_win else 'make'} ===")

    env: dict[str, str] | None = None
    if is_win and build_system == "ninja":
        env = _ensure_msvc_env()

    if is_mac:
        _check_metal_toolchain()

    need_configure = force_reconfigure or not cache_file.exists()
    if not need_configure:
        cached = _cache_build_type(cache_file)
        if cached and cached != config:
            need_configure = True
    if not need_configure:
        missing_build_file = _missing_generated_build_file(cache_file, bd, config)
        if missing_build_file is not None:
            print(f"\n> CMake cache exists but generated build file is missing: {missing_build_file}")
            need_configure = True

    if need_configure:
        rc = run(["cmake", "--preset", preset], root, env=env)
        if rc != 0:
            return rc, bd, config, env
    else:
        print(f"\n> using existing CMake cache: {cache_file}")

    build_cmd = ["cmake", "--build", str(bd), "--config", config, "--target", "draxul", "--parallel"]
    rc = run(build_cmd, root, env=env)
    return rc, bd, config, env


def cmd_build(root: pathlib.Path, args: list[str]) -> int:
    """Configure + build only (no run)."""
    mode, force_reconfigure, build_system, _, _ = _parse_build_args(args)
    rc, _, _, _ = _configure_and_build(root, mode, force_reconfigure, build_system)
    return rc


def cmd_run(root: pathlib.Path, args: list[str]) -> int:
    """Full configure + build + run cycle (replaces r.bat / r.sh)."""
    mode, force_reconfigure, build_system, use_console, app_args = _parse_build_args(args)
    try:
        app_args, megacity_parser = _consume_megacity_parser_args(app_args)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    rc, bd, config, env = _configure_and_build(root, mode, force_reconfigure, build_system)
    if rc != 0:
        return rc

    exe = draxul_exe(bd, config)
    if not exe.exists():
        print(f"\nMissing executable: {exe}")
        return 1

    if megacity_parser is not None:
        config_path = _apply_megacity_parser_config(megacity_parser)
        print(f"\n> megacity parser: {megacity_parser} ({config_path})")

    is_win = sys.platform.startswith("win")
    cmd: list[str] = [str(exe)] + app_args
    if is_win and not use_console:
        print(f"\n> start /wait {' '.join(cmd)}")
        proc = subprocess.run(["cmd", "/c", "start", "", "/wait"] + cmd, cwd=root, check=False, env=env)
        return proc.returncode
    else:
        return run(cmd, root, env=env)


def _deploy_platform_name(platform: str = sys.platform) -> str:
    if platform.startswith("win"):
        return "win"
    if platform == "darwin":
        return "mac"
    raise RuntimeError("deploy is currently supported only on macOS and Windows")


def _deploy_date_label(now: datetime | None = None) -> str:
    return (now or datetime.now()).strftime("%Y_%m_%d")


def _deploy_output_paths(
    root: pathlib.Path,
    date_label: str,
    platform: str = sys.platform,
) -> tuple[pathlib.Path, pathlib.Path]:
    platform_name = _deploy_platform_name(platform)
    date_dir = root / "deploy" / date_label
    platform_dir = date_dir / platform_name
    archive_path = date_dir / f"draxul-{date_label}-{platform_name}.zip"
    return platform_dir, archive_path


def _deploy_payload_source(bd: pathlib.Path, config: str, platform: str = sys.platform) -> pathlib.Path:
    if platform.startswith("win"):
        config_exe = bd / config / "draxul.exe"
        return config_exe if config_exe.exists() else bd / "draxul.exe"
    if platform == "darwin":
        bundle = bd / "draxul.app"
        if bundle.exists():
            return bundle
        return bd / "draxul"
    raise RuntimeError("deploy is currently supported only on macOS and Windows")


def _parse_deploy_args(args: list[str]) -> tuple[bool, str]:
    mode, force_reconfigure, build_system, _, app_args = _parse_build_args(args)
    requested_modes = [arg.lower() for arg in args if arg.lower() in ("debug", "release", "relwithdebinfo")]
    if app_args:
        raise ValueError("deploy accepts build flags only: [release] [--reconfigure] [--vs|--ninja]")
    if requested_modes and mode != "release":
        raise ValueError("deploy always creates a release build; use `do deploy` or `do deploy release`")
    return force_reconfigure, build_system


def _stage_deploy_payload(
    source: pathlib.Path,
    platform_dir: pathlib.Path,
    archive_path: pathlib.Path,
    windows_runtime_directory: pathlib.Path | None = None,
) -> None:
    if platform_dir.exists():
        def remove_readonly(function, path, _error_info):
            os.chmod(path, stat.S_IWRITE)
            function(path)

        shutil.rmtree(platform_dir, onerror=remove_readonly)
    platform_dir.mkdir(parents=True, exist_ok=True)

    if source.is_dir() and source.suffix == ".app":
        shutil.copytree(source, platform_dir / source.name)
    elif source.suffix.lower() == ".exe":
        shutil.copy2(source, platform_dir / source.name)
        for directory_name in ("assets", "fonts", "shaders"):
            runtime_directory = source.parent / directory_name
            if runtime_directory.is_dir():
                shutil.copytree(runtime_directory, platform_dir / directory_name)
        for runtime_library in sorted(source.parent.glob("*.dll")):
            shutil.copy2(runtime_library, platform_dir / runtime_library.name)
        if windows_runtime_directory is None and sys.platform.startswith("win"):
            system_root = os.environ.get("SystemRoot")
            if system_root:
                windows_runtime_directory = pathlib.Path(system_root) / "System32"
        if windows_runtime_directory is not None:
            for library_name in WINDOWS_CRT_RUNTIME_LIBRARIES:
                destination = platform_dir / library_name
                if destination.exists():
                    continue
                runtime_library = windows_runtime_directory / library_name
                if not runtime_library.is_file():
                    raise FileNotFoundError(f"Missing Windows runtime library: {runtime_library}")
                shutil.copy2(runtime_library, destination)
    elif source.is_dir():
        for child in source.iterdir():
            destination = platform_dir / child.name
            if child.is_dir():
                shutil.copytree(child, destination)
            else:
                shutil.copy2(child, destination)
    else:
        shutil.copy2(source, platform_dir / source.name)

    if archive_path.exists():
        archive_path.unlink()
    archive_base = archive_path.with_suffix("")
    shutil.make_archive(
        str(archive_base),
        "zip",
        root_dir=platform_dir.parent,
        base_dir=platform_dir.name,
    )


def cmd_deploy(root: pathlib.Path, args: list[str]) -> int:
    """Build Release and write a compressed deploy package."""
    try:
        force_reconfigure, build_system = _parse_deploy_args(args)
    except ValueError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    rc, bd, config, _ = _configure_and_build(root, "release", force_reconfigure, build_system)
    if rc != 0:
        return rc

    source = _deploy_payload_source(bd, config)
    if not source.exists():
        print(f"Missing deploy payload: {source}", file=sys.stderr)
        return 1

    date_label = _deploy_date_label()
    platform_dir, archive_path = _deploy_output_paths(root, date_label)
    try:
        _stage_deploy_payload(source, platform_dir, archive_path)
    except OSError as error:
        print(f"Failed to stage deploy payload: {error}", file=sys.stderr)
        return 1
    print(f"\nDeploy folder:  {platform_dir}")
    print(f"Deploy archive: {archive_path}")
    return 0


def scenario_path(root: pathlib.Path, name: str) -> pathlib.Path:
    return root / "tests" / "render" / f"{name}.toml"


def platform_suffix() -> str:
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform.startswith("darwin"):
        return "macos"
    return "linux"


def print_render_report(root: pathlib.Path, scenario_name: str) -> None:
    report_path = root / "tests" / "render" / "out" / f"{scenario_name}.{platform_suffix()}.report.json"
    if not report_path.exists():
        print(f"  [no report found: {report_path}]")
        return
    try:
        data = json.loads(report_path.read_text())
    except Exception as e:
        print(f"  [failed to read report: {e}]")
        return

    if "error" in data:
        print(f"  [{scenario_name}] ERROR: {data['error']}")
        return

    if "changed_pixels_pct" in data:
        passed = data.get("passed", False)
        label = "PASS" if passed else "FAIL"
        print(
            f"  [{scenario_name}] diff: {data['changed_pixels_pct']:.4f}% changed pixels"
            f" ({data['changed_pixels']}/{data['width'] * data['height']})"
            f", max_channel_delta={data['max_channel_diff']}"
            f", mean_abs={data['mean_abs_channel_diff']:.4f}"
            f" [{label}]"
        )
    elif data.get("blessed"):
        print(f"  [{scenario_name}] blessed ({data['width']}x{data['height']})")


def run(command: list[str], cwd: pathlib.Path, *, env: dict[str, str] | None = None) -> int:
    print("> " + " ".join(command))
    completed = subprocess.run(command, cwd=cwd, check=False, env=env)
    return completed.returncode


def command_text(command: list[str]) -> str:
    return " ".join(shlex.quote(str(part)) for part in command)


def _unexpected_review_arg(arg: str) -> None:
    print(f"Unexpected review argument: {arg}\n")
    print(help_text())
    raise SystemExit(2)


def parse_review_args(args: list[str]) -> SimpleNamespace:
    review_target = "all"
    agy_timeout_seconds = 900
    dry_run = False
    index = 0

    while index < len(args):
        arg = args[index]
        lowered = arg.lower()
        if lowered in REVIEW_MODES:
            if review_target != "all":
                _unexpected_review_arg(arg)
            review_target = lowered
            index += 1
            continue
        if arg == "--dry-run":
            dry_run = True
            index += 1
            continue
        if arg in {"--agy-timeout", "--agy-timeout-seconds"}:
            if index + 1 >= len(args):
                _unexpected_review_arg(arg)
            try:
                agy_timeout_seconds = int(args[index + 1])
            except ValueError:
                _unexpected_review_arg(args[index + 1])
            index += 2
            continue
        for prefix in ("--agy-timeout=", "--agy-timeout-seconds="):
            if arg.startswith(prefix):
                try:
                    agy_timeout_seconds = int(arg[len(prefix):])
                except ValueError:
                    _unexpected_review_arg(arg)
                index += 1
                break
        else:
            _unexpected_review_arg(arg)

    if agy_timeout_seconds <= 0:
        print("agy timeout must be positive\n")
        print(help_text())
        raise SystemExit(2)

    return SimpleNamespace(
        review_target=review_target,
        agy_timeout_seconds=agy_timeout_seconds,
        dry_run=dry_run,
    )


def parse_consensus_args(args: list[str]) -> SimpleNamespace:
    dry_run = False
    for arg in args:
        lowered = arg.lower()
        if arg == "--dry-run":
            dry_run = True
            continue
        if lowered in {"codex", "gpt"}:
            continue
        if lowered in {"claude", "gemini", "agy"}:
            print("Consensus now runs through Codex only. Use `do consensus [--dry-run]`.\n")
            print(help_text())
            raise SystemExit(2)
        _unexpected_review_arg(arg)
    return SimpleNamespace(dry_run=dry_run)


def _consensus_basename_for_prompt(prompt_stem: str) -> str:
    if prompt_stem == "review":
        return "review-consensus"
    if prompt_stem == "review_bugs":
        return "review-bugs-consensus"
    return f"{prompt_stem.replace('_', '-')}-consensus"


def create_review_plan(
    root: pathlib.Path,
    agy_timeout_seconds: int = 900,
    review_target: str = "all",
    *,
    prompt_stem: str = "review",
    review_basename: str = "review-latest",
    consensus_basename: str | None = None,
) -> SimpleNamespace:
    if agy_timeout_seconds <= 0:
        raise ValueError("agy_timeout_seconds must be positive")

    normalized_target = review_target.lower()
    if normalized_target not in REVIEW_MODES:
        raise ValueError(f"Unsupported review target: {review_target}")

    if consensus_basename is None:
        consensus_basename = _consensus_basename_for_prompt(prompt_stem)

    reviews_dir = root / "plans" / "reviews"
    review_prompt_path = root / "plans" / "prompts" / f"{prompt_stem}.md"
    consensus_prompt_name = "consensus_review.md" if prompt_stem == "review" else f"consensus_{prompt_stem}.md"
    consensus_prompt_path = root / "plans" / "prompts" / consensus_prompt_name
    codex_review_path = reviews_dir / f"{review_basename}.gpt.md"
    gemini_review_path = reviews_dir / f"{review_basename}.gemini.md"
    claude_review_path = reviews_dir / f"{review_basename}.claude.md"
    consensus_review_path = reviews_dir / f"{consensus_basename}.md"
    codex_run_log_path = reviews_dir / f"{review_basename}.gpt.run.log"
    gemini_run_log_path = reviews_dir / f"{review_basename}.gemini.agy.log"
    gemini_stdio_log_path = reviews_dir / f"{review_basename}.gemini.stdio.log"
    claude_run_json_path = reviews_dir / f"{review_basename}.claude.run.json"
    consensus_codex_message_path = reviews_dir / f"{consensus_basename}.codex-message.md"
    consensus_run_log_path = reviews_dir / f"{consensus_basename}.codex.run.log"
    review_script_path = root / "scripts" / "Run-Review.ps1"

    return SimpleNamespace(
        repo_root=root,
        reviews_dir=reviews_dir,
        review_prompt_path=review_prompt_path,
        consensus_prompt_path=consensus_prompt_path,
        codex_review_path=codex_review_path,
        gemini_review_path=gemini_review_path,
        claude_review_path=claude_review_path,
        consensus_review_path=consensus_review_path,
        codex_run_log_path=codex_run_log_path,
        gemini_run_log_path=gemini_run_log_path,
        gemini_stdio_log_path=gemini_stdio_log_path,
        claude_run_json_path=claude_run_json_path,
        consensus_codex_message_path=consensus_codex_message_path,
        consensus_run_log_path=consensus_run_log_path,
        review_script_path=review_script_path,
        mode=REVIEW_MODES[normalized_target],
        agy_timeout_seconds=agy_timeout_seconds,
    )


def create_consensus_plan(
    root: pathlib.Path,
    *,
    prompt_stem: str = "review",
    review_basename: str = "review-latest",
    consensus_basename: str | None = None,
) -> SimpleNamespace:
    plan = create_review_plan(
        root,
        prompt_stem=prompt_stem,
        review_basename=review_basename,
        consensus_basename=consensus_basename,
    )
    plan.mode = "Consensus"
    return plan


def resolve_required_command(command: str, install_hint: str, dry_run: bool = False) -> str:
    resolved = shutil.which(command)
    if resolved:
        return resolved
    if dry_run:
        return command
    raise RuntimeError(f"Could not find {command} on PATH. {install_hint}")


def resolve_codex_command(dry_run: bool = False) -> str:
    if sys.platform.startswith("win"):
        application = None
        for candidate in ("codex.cmd", "codex.exe"):
            resolved = shutil.which(candidate)
            if resolved:
                return resolved
        shim = shutil.which("codex")
        if shim and pathlib.Path(shim).suffix.lower() in {".exe", ".cmd"}:
            application = shim
        if application:
            return application
        if shim and not dry_run:
            raise RuntimeError(
                f"Found {shim}, but review needs codex.cmd or codex.exe to avoid PowerShell stderr shim failures."
            )
        if dry_run:
            return "codex"
        raise RuntimeError("Could not find codex.cmd or codex.exe on PATH. Install or add the Codex CLI to PATH.")
    return resolve_required_command("codex", "Install or add the Codex CLI to PATH.", dry_run)


def resolve_agy_command(dry_run: bool = False) -> str:
    resolved = shutil.which("agy")
    if resolved:
        return resolved

    if sys.platform.startswith("win"):
        local_app_data = os.environ.get("LOCALAPPDATA")
        fallback = pathlib.Path(local_app_data) / "agy" / "bin" / "agy.exe" if local_app_data else None
        if fallback and fallback.exists():
            return str(fallback)
        if dry_run:
            return "agy"
        fallback_text = str(fallback) if fallback else r"%LOCALAPPDATA%\agy\bin\agy.exe"
        raise RuntimeError(f"Could not find agy on PATH or at {fallback_text}.")

    if dry_run:
        return "agy"
    raise RuntimeError("Could not find agy on PATH.")


def run_native_command(
    command: list[str],
    cwd: pathlib.Path,
    *,
    input_text: str | None = None,
    log_path: pathlib.Path | None = None,
    dry_run: bool = False,
    display_command: list[str] | None = None,
) -> SimpleNamespace:
    print("> " + command_text(display_command or command))
    if dry_run:
        return SimpleNamespace(returncode=0, stdout="", stderr="", output="", output_lines=[])

    input_bytes = input_text.encode("utf-8") if input_text is not None else None
    result = subprocess.run(
        command,
        cwd=cwd,
        input=input_bytes,
        capture_output=True,
        check=False,
    )
    stdout = (result.stdout or b"").decode("utf-8", errors="replace")
    stderr = (result.stderr or b"").decode("utf-8", errors="replace")
    output = stdout + stderr

    if log_path is not None:
        log_path.write_text(output, encoding="utf-8")
    if output:
        console_text = output if output.endswith("\n") else output + "\n"
        sys.stdout.buffer.write(console_text.encode(sys.stdout.encoding or "utf-8", errors="replace"))
        sys.stdout.flush()

    return SimpleNamespace(
        returncode=result.returncode,
        stdout=stdout,
        stderr=stderr,
        output=output,
        output_lines=output.splitlines(),
    )


def assert_path_exists(path: pathlib.Path, description: str) -> None:
    if not path.exists():
        raise RuntimeError(f"{description} not found: {path}")


def ensure_review_workspace(plan: SimpleNamespace, dry_run: bool) -> None:
    if dry_run:
        return

    plan.reviews_dir.mkdir(parents=True, exist_ok=True)
    for folder in ("ice-box", "pending", "done"):
        (plan.repo_root / "kanban" / folder).mkdir(parents=True, exist_ok=True)


def assert_meaningful_review(path: pathlib.Path, name: str) -> None:
    if not path.exists():
        raise RuntimeError(f"{name} review did not produce {path}")

    text = path.read_text(encoding="utf-8", errors="ignore")
    if not text.strip():
        raise RuntimeError(f"{name} review output is empty: {path}")

    if len(text.strip()) < 500:
        raise RuntimeError(f"{name} review output is too short to be a useful review: {path}")

    for pattern in REVIEW_FAILURE_PATTERNS:
        if pattern in text:
            raise RuntimeError(f"{name} review output looks like a failure ({pattern}): {path}")


def clean_agy_output(output_lines: list[str]) -> list[str]:
    ignored = {
        "YOLO mode is enabled. All tool calls will be automatically approved.",
        "Loaded cached credentials.",
    }
    return [line for line in output_lines if line and line.strip() not in ignored]


def review_prompt_with_instruction(prompt_path: pathlib.Path, instruction: str) -> str:
    review_prompt = prompt_path.read_text(encoding="utf-8")
    return f"{review_prompt}\n\n{instruction}\n"


def reset_review_output(path: pathlib.Path, dry_run: bool) -> None:
    if not dry_run and path.exists():
        path.unlink()


def run_codex_review(plan: SimpleNamespace, dry_run: bool) -> None:
    print("Running Codex review...")
    codex_command = resolve_codex_command(dry_run)
    prompt = review_prompt_with_instruction(
        plan.review_prompt_path,
        "Host instruction: perform review only. Do not edit repository files. Return the full review as markdown.",
    )
    command = [
        codex_command,
        "exec",
        "--skip-git-repo-check",
        "-C",
        str(plan.repo_root),
        "--model",
        CODEX_REVIEW_MODEL,
        "--sandbox",
        "danger-full-access",
        "-o",
        str(plan.codex_review_path),
        "-",
    ]
    result = run_native_command(command, plan.repo_root, input_text=prompt, log_path=plan.codex_run_log_path, dry_run=dry_run)
    if dry_run:
        return
    if result.returncode != 0:
        raise RuntimeError(f"Codex review failed with exit code {result.returncode}. See {plan.codex_run_log_path}")
    assert_meaningful_review(plan.codex_review_path, "Codex")


def run_gemini_review(plan: SimpleNamespace, dry_run: bool) -> None:
    print("Running Gemini review through agy...")
    agy_command = resolve_agy_command(dry_run)
    reset_review_output(plan.gemini_run_log_path, dry_run)
    reset_review_output(plan.gemini_stdio_log_path, dry_run)
    reset_review_output(plan.gemini_review_path, dry_run)

    prompt = review_prompt_with_instruction(
        plan.review_prompt_path,
        "\n".join(
            [
                "Host instruction: perform review only. Write the complete review to this exact file path:",
                str(plan.gemini_review_path),
                "",
                "Also print the same markdown to stdout. Do not modify any other repository files.",
            ]
        ),
    )
    command = [
        agy_command,
        "--log-file",
        str(plan.gemini_run_log_path),
        "--print",
        prompt,
        "--print-timeout",
        f"{plan.agy_timeout_seconds}s",
        "--dangerously-skip-permissions",
    ]
    display_command = [
        agy_command,
        "--log-file",
        str(plan.gemini_run_log_path),
        "--print",
        "<review prompt>",
        "--print-timeout",
        f"{plan.agy_timeout_seconds}s",
        "--dangerously-skip-permissions",
    ]
    result = run_native_command(
        command,
        plan.repo_root,
        log_path=plan.gemini_stdio_log_path,
        dry_run=dry_run,
        display_command=display_command,
    )
    if dry_run:
        return

    if result.returncode != 0:
        output_text = result.output.strip()
        failure_text = f"""# Gemini review failed

Agy exited with code {result.returncode}.

Stdio log: {plan.gemini_stdio_log_path}
Agy log: {plan.gemini_run_log_path}

Output:

{output_text}
"""
        plan.gemini_review_path.write_text(failure_text, encoding="utf-8")
        raise RuntimeError(f"Gemini review failed with exit code {result.returncode}. See {plan.gemini_review_path}")

    if plan.gemini_review_path.exists():
        try:
            assert_meaningful_review(plan.gemini_review_path, "Gemini")
            return
        except RuntimeError as error:
            print(f"Agy wrote {plan.gemini_review_path}, but it did not validate: {error}")

    clean_output = clean_agy_output(result.output_lines)
    if not clean_output:
        log_text = plan.gemini_run_log_path.read_text(encoding="utf-8", errors="ignore") if plan.gemini_run_log_path.exists() else ""
        log_lines = log_text.splitlines()
        log_tail = "\n".join(log_lines[-80:]) if log_lines else "<no agy log was written>"
        failure_text = f"""# Gemini review failed

Agy produced no stdout.

Stdio log: {plan.gemini_stdio_log_path}
Agy log: {plan.gemini_run_log_path}

Log tail:

{log_tail}
"""
        plan.gemini_review_path.write_text(failure_text, encoding="utf-8")
        raise RuntimeError(f"Gemini review failed because agy produced no stdout. See {plan.gemini_review_path}")

    plan.gemini_review_path.write_text("\n".join(clean_output) + "\n", encoding="utf-8")
    assert_meaningful_review(plan.gemini_review_path, "Gemini")


def find_claude_plan_path(text: str) -> pathlib.Path | None:
    patterns = (
        r"[A-Za-z]:\\[^\r\n\"`]*?\\.claude\\plans\\[^\r\n\"`]+?\.md",
        r"/[^\r\n\"`]*?\.claude/plans/[^\r\n\"`]+?\.md",
        r"~[^\r\n\"`]*?\.claude/plans/[^\r\n\"`]+?\.md",
    )
    for pattern in patterns:
        match = re.search(pattern, text)
        if match:
            path = pathlib.Path(match.group(0)).expanduser()
            if path.exists():
                return path
    return None


def run_claude_review(plan: SimpleNamespace, dry_run: bool) -> None:
    print("Running Claude review...")
    claude_command = resolve_required_command("claude", "Install or add the Claude CLI to PATH.", dry_run)
    prompt = review_prompt_with_instruction(
        plan.review_prompt_path,
        "Host instruction: perform review only. Do not edit repository files. Return the full review as markdown in your final result.",
    )
    command = [
        claude_command,
        "-p",
        "--model",
        CLAUDE_REVIEW_MODEL,
        "--output-format",
        "json",
        "--permission-mode",
        "bypassPermissions",
        "--allowedTools",
        "Read",
        "Grep",
        "Glob",
        "Bash",
    ]
    result = run_native_command(command, plan.repo_root, input_text=prompt, dry_run=dry_run)
    if dry_run:
        return

    plan.claude_run_json_path.write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"Claude review failed with exit code {result.returncode}. See {plan.claude_run_json_path}")

    try:
        claude_json = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"Claude review returned invalid JSON. See {plan.claude_run_json_path}") from error

    claude_result = str(claude_json.get("result", ""))
    claude_plan_path = find_claude_plan_path(claude_result)
    if claude_plan_path is not None:
        shutil.copy2(claude_plan_path, plan.claude_review_path)
    else:
        plan.claude_review_path.write_text(claude_result, encoding="utf-8")

    assert_meaningful_review(plan.claude_review_path, "Claude")


def run_consensus_review(plan: SimpleNamespace, dry_run: bool) -> None:
    print("Running consensus review...")
    codex_command = resolve_codex_command(dry_run)
    consensus_prompt = plan.consensus_prompt_path.read_text(encoding="utf-8")
    command = [
        codex_command,
        "exec",
        "--skip-git-repo-check",
        "-C",
        str(plan.repo_root),
        "--model",
        CODEX_REVIEW_MODEL,
        "--sandbox",
        "danger-full-access",
        "-o",
        str(plan.consensus_codex_message_path),
        "-",
    ]
    result = run_native_command(command, plan.repo_root, input_text=consensus_prompt, log_path=plan.consensus_run_log_path, dry_run=dry_run)
    if dry_run:
        return
    if result.returncode != 0:
        raise RuntimeError(f"Consensus review failed with exit code {result.returncode}. See {plan.consensus_run_log_path}")
    assert_meaningful_review(plan.consensus_review_path, "Consensus")


def run_review(plan: SimpleNamespace, dry_run: bool) -> int:
    try:
        assert_path_exists(plan.review_prompt_path, "Review prompt")
        assert_path_exists(plan.consensus_prompt_path, "Consensus prompt")
        ensure_review_workspace(plan, dry_run)

        if plan.mode == "All":
            run_codex_review(plan, dry_run)
            run_gemini_review(plan, dry_run)
            run_claude_review(plan, dry_run)
            print("All reviews succeeded; running consensus...")
            run_consensus_review(plan, dry_run)
        elif plan.mode == "Codex":
            run_codex_review(plan, dry_run)
        elif plan.mode == "Agy":
            run_gemini_review(plan, dry_run)
        elif plan.mode == "Claude":
            run_claude_review(plan, dry_run)
        elif plan.mode == "Consensus":
            run_consensus_review(plan, dry_run)
        else:
            raise RuntimeError(f"Unsupported review mode: {plan.mode}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    return 0


def build_shortcut_exe(root: pathlib.Path) -> tuple[int, pathlib.Path | None, dict[str, str] | None]:
    """Build the app for smoke/render shortcuts using the current default pipeline."""
    rc, bd, config, env = _configure_and_build(root, "debug", False, "ninja")
    if rc != 0:
        return rc, None, env

    exe = draxul_exe(bd, config)
    if not exe.exists():
        print(f"\nMissing executable: {exe}")
        return 1, None, env
    return 0, exe, env


def ensure_built(root: pathlib.Path) -> int:
    exe = draxul_path(root)
    if exe.exists():
        return 0

    if sys.platform.startswith("win"):
        return run(["cmake", "--build", str(build_dir(root)), "--config", "Release", "--parallel"], root)
    return run(["cmake", "--build", str(build_dir(root)), "--parallel"], root)


def help_text() -> str:
    return """Usage:
  do <command> [options]

Single-word shortcuts:
  build [debug|release|relwithdebinfo] [--reconfigure] [--vs|--ninja]
               Configure and build Draxul (default: debug, ninja on Windows)
  run [debug|release|relwithdebinfo] [--reconfigure] [--vs|--ninja] [--console]
      [--host megacity --parser treesitter|treesitter_db] [-- app-args...]
               Configure, build, and run Draxul
  deploy [release] [--reconfigure] [--vs|--ninja]
               Build Release and package deploy/YYYY_MM_DD/mac|win plus a zip archive
  smoke        Run the app smoke test
  test         Run the full local test suite (t.bat / run_tests.sh)
  shot         Regenerate the README hero screenshot
  api          Build local Doxygen API docs
  docs         Build all docs artifacts
  review [all|codex|agy|gemini|claude] [--agy-timeout seconds] [--dry-run]
               Run AI code review with the native Codex/Claude/Agy flow
  review-bugs [all|codex|agy|gemini|claude] [--agy-timeout seconds] [--dry-run]
               Run bug-focused AI review with the native Codex/Claude/Agy flow
  review-codex Run only the Codex reviewer
  review-gemini  Run only the Gemini reviewer
  review-claude  Run only the Claude reviewer
  review-gpt     Alias for review-codex
  consensus [--dry-run]
               Run consensus synthesis on the latest reviews through Codex
  consensus-bugs [--dry-run]
               Run bug triage consensus on the latest bug reviews through Codex
  coverage     macOS: build with LLVM coverage, export build/coverage.lcov, copy to db/coverage.lcov
  syncboard    Sync work-items and icebox to the GitHub project board

Deterministic render snapshots:
  basic        Run basic-view compare
  cmdline      Run cmdline-view compare
  unicode      Run unicode-view compare
  panel        Run panel-view compare
  nanovg       Run nanovg-demo compare
  renderall    Run all compare snapshots

Bless render references:
  blessbasic   Bless basic-view
  blesscmdline Bless cmdline-view
  blessunicode Bless unicode-view
  blesspanel   Bless panel-view
  blessnanovg  Bless nanovg-demo
  blessall     Bless all deterministic references

Examples:
  do build relwithdebinfo  # Optimized build + symbols (Windows)
  do run                   # Debug build + run (ninja on Windows, make on macOS)
  do run release           # Release build + run
  do run relwithdebinfo    # Release-ish build + symbols (Windows)
  do run release --vs      # Release build with VS generator (Windows)
  do run release --host megacity --parser treesitter
                             # MegaCity with Tree-sitter semantic source config
  do deploy                 # Release build + deploy/YYYY_MM_DD/mac|win zip package
  do run --reconfigure     # Force CMake reconfigure
  do smoke
  do basic
  do blessall
"""


def main() -> int:
    root = repo_root()
    args = sys.argv[1:]

    if not args or args[0] in {"-h", "--help", "help"}:
        print(help_text())
        return 0

    command = args[0].lower()
    skip_build = "--skip-build" in args[1:]

    if command == "test":
        if sys.platform.startswith("win"):
            return run(["cmd", "/c", "t.bat"], root)
        return run(["sh", "./scripts/run_tests.sh"], root)

    if command == "shot":
        cmd = [sys.executable, str(root / "scripts" / "update_screenshot.py")]
        if skip_build:
            cmd.append("--skip-build")
        return run(cmd, root)

    if command == "api":
        return run([sys.executable, str(root / "scripts" / "build_docs.py"), "--api-only"], root)

    if command == "docs":
        return run([sys.executable, str(root / "scripts" / "build_docs.py")], root)

    if command == "review":
        parsed = parse_review_args(args[1:])
        plan = create_review_plan(root, parsed.agy_timeout_seconds, parsed.review_target)
        return run_review(plan, parsed.dry_run)

    if command == "review-bugs":
        parsed = parse_review_args(args[1:])
        plan = create_review_plan(
            root,
            parsed.agy_timeout_seconds,
            parsed.review_target,
            prompt_stem="review_bugs",
            review_basename="review-bugs-latest",
            consensus_basename="review-bugs-consensus",
        )
        return run_review(plan, parsed.dry_run)

    if command in {"review-gemini", "review-claude", "review-gpt", "review-codex"}:
        agent = command.split("-", 1)[1].lower()
        review_target = {
            "gemini": "gemini",
            "claude": "claude",
            "codex": "codex",
            "gpt": "gpt",
        }[agent]
        parsed = parse_review_args([review_target, *args[1:]])
        plan = create_review_plan(root, parsed.agy_timeout_seconds, parsed.review_target)
        return run_review(plan, parsed.dry_run)

    if command == "consensus":
        parsed = parse_consensus_args(args[1:])
        plan = create_consensus_plan(root)
        return run_review(plan, parsed.dry_run)

    if command == "consensus-bugs":
        parsed = parse_consensus_args(args[1:])
        plan = create_consensus_plan(
            root,
            prompt_stem="review_bugs",
            review_basename="review-bugs-latest",
            consensus_basename="review-bugs-consensus",
        )
        return run_review(plan, parsed.dry_run)

    if command == "coverage":
        if not sys.platform.startswith("darwin"):
            print("ERROR: coverage export is currently supported only on macOS; local coverage writes build/coverage.lcov and refreshes db/coverage.lcov.")
            return 1
        bd = build_dir(root)
        # 1. Configure with coverage preset
        rc = run(["cmake", "--preset", "mac-coverage"], root)
        if rc != 0:
            return rc
        # 2. Build test binary
        rc = run(["cmake", "--build", str(bd), "--target", "draxul-tests", "draxul-rpc-fake"], root)
        if rc != 0:
            return rc
        # 3. Run tests under coverage instrumentation
        import os
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = str(bd / "coverage-%p.profraw")
        print(f"> ctest --test-dir {bd} -R draxul-tests --output-on-failure")
        rc = subprocess.run(
            ["ctest", "--test-dir", str(bd), "-R", "draxul-tests", "--output-on-failure"],
            env=env, cwd=root, check=False,
        ).returncode
        if rc != 0:
            return rc
        # 4. Merge raw profiles
        import glob as globmod
        profraw_files = globmod.glob(str(bd / "coverage-*.profraw"))
        if not profraw_files:
            print("ERROR: no .profraw files found")
            return 1
        profdata = bd / "coverage.profdata"
        rc = run(["xcrun", "llvm-profdata", "merge", "-sparse"] + profraw_files + ["-o", str(profdata)], root)
        if rc != 0:
            return rc
        # 5. Export LCOV
        lcov_file = bd / "coverage.lcov"
        test_exe = bd / "tests" / "draxul-tests"
        rc = subprocess.run(
            ["xcrun", "llvm-cov", "export", str(test_exe),
             f"--instr-profile={profdata}",
             "--format=lcov",
             "--ignore-filename-regex=(build/_deps|tests/)"],
            stdout=open(lcov_file, "w"), cwd=root, check=False,
        ).returncode
        if rc != 0:
            return rc
        import shutil
        db_lcov_file = root / "db" / "coverage.lcov"
        shutil.copyfile(lcov_file, db_lcov_file)
        print(f"\nCoverage report written to: {lcov_file}")
        print(f"Coverage report copied to:  {db_lcov_file}")
        # Quick summary
        fn_total = 0
        fn_hit = 0
        for line in open(lcov_file):
            if line.startswith("FNF:"):
                fn_total += int(line[4:].strip())
            elif line.startswith("FNH:"):
                fn_hit += int(line[4:].strip())
        if fn_total > 0:
            print(f"Functions: {fn_hit}/{fn_total} ({fn_hit * 100.0 / fn_total:.1f}%)")
        return 0

    if command == "syncboard":
        return run([sys.executable, str(root / "scripts" / "sync_project_board.py")], root)

    if command == "build":
        return cmd_build(root, args[1:])

    if command == "run":
        return cmd_run(root, args[1:])

    if command == "deploy":
        return cmd_deploy(root, args[1:])

    if command == "smoke":
        rc, exe, env = build_shortcut_exe(root)
        if rc != 0 or exe is None:
            return 1
        return run([str(exe), "--console", "--smoke-test"], root, env=env)

    render_map = {
        "basic": ("basic-view", False),
        "cmdline": ("cmdline-view", False),
        "unicode": ("unicode-view", False),
        "panel": ("panel-view", False),
        "blessbasic": ("basic-view", True),
        "blesscmdline": ("cmdline-view", True),
        "blessunicode": ("unicode-view", True),
        "blesspanel": ("panel-view", True),
        "nanovg": ("nanovg-demo", False),
        "blessnanovg": ("nanovg-demo", True),
    }

    if command in render_map:
        rc, exe, env = build_shortcut_exe(root)
        if rc != 0 or exe is None:
            return 1
        scenario_name, bless = render_map[command]
        cmd = [str(exe), "--console", "--render-test", str(scenario_path(root, scenario_name)),
               "--show-render-test-window"]
        if bless:
            cmd.append("--bless-render-test")
        rc = run(cmd, root, env=env)
        print_render_report(root, scenario_name)
        return rc

    if command == "renderall":
        rc, exe, env = build_shortcut_exe(root)
        if rc != 0 or exe is None:
            return 1
        overall_rc = 0
        for scenario_name in ("basic-view", "cmdline-view", "unicode-view", "panel-view", "nanovg-demo"):
            rc = run([str(exe), "--console", "--render-test",
                      str(scenario_path(root, scenario_name)), "--show-render-test-window"], root, env=env)
            print_render_report(root, scenario_name)
            if rc != 0:
                overall_rc = rc
        return overall_rc

    if command == "blessall":
        rc, exe, env = build_shortcut_exe(root)
        if rc != 0 or exe is None:
            return 1
        for scenario_name in ("basic-view", "cmdline-view", "unicode-view", "panel-view", "nanovg-demo"):
            rc = run(
                [str(exe), "--console", "--render-test",
                 str(scenario_path(root, scenario_name)), "--show-render-test-window", "--bless-render-test"],
                root,
                env=env,
            )
            if rc != 0:
                return rc
        return 0

    # If the "command" looks like a flag, the user probably meant `run <flags>`.
    if command.startswith("-"):
        return cmd_run(root, args)

    print(f"Unknown command: {command}\n")
    print(help_text())
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
