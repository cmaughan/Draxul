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
import tempfile
import sys
from datetime import datetime

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


def build_dirs(root: pathlib.Path) -> list[pathlib.Path]:
    """Return repository-root build trees owned by Draxul's build workflows."""
    candidates = [build_dir(root), *sorted(root.glob("build-*"))]
    return [path for path in candidates if path.is_dir() and not path.is_symlink()]


def _remove_tree(path: pathlib.Path) -> None:
    def remove_readonly(function, failed_path, _error_info):
        os.chmod(failed_path, stat.S_IWRITE)
        function(failed_path)

    shutil.rmtree(path, onerror=remove_readonly)


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


def _parallel_jobs() -> str:
    """Bounded build parallelism. A bare `cmake --build --parallel` maps to an
    unlimited `make -j` with the Makefiles generator; a job storm across large
    third-party deps (e.g. Verovio's ~400 files) can swamp the machine."""
    return str(os.cpu_count() or 8)


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

    build_cmd = ["cmake", "--build", str(bd), "--config", config, "--target", "draxul", "--parallel", _parallel_jobs()]
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
        _remove_tree(platform_dir)
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


RENDER_SCENARIO_FIELDS = {
    "name",
    "purpose",
    "status",
    "platforms",
    "ctest",
    "reference_required",
    "renderall",
    "blessall",
    "compare_command",
    "bless_command",
}


def load_render_manifest(root: pathlib.Path, *, validate_files: bool = True) -> list[dict]:
    manifest_path = root / "tests" / "render" / "manifest.json"
    try:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid render manifest {manifest_path}: {exc}") from exc

    if set(document) != {"version", "scenarios"} or document.get("version") != 1:
        raise ValueError("render manifest must contain only version=1 and scenarios")
    scenarios = document.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        raise ValueError("render manifest scenarios must be a non-empty list")

    names: set[str] = set()
    commands: set[str] = set()
    for index, scenario in enumerate(scenarios):
        if not isinstance(scenario, dict):
            raise ValueError(f"render scenario #{index} must be an object")
        unknown = set(scenario) - RENDER_SCENARIO_FIELDS
        missing = RENDER_SCENARIO_FIELDS - set(scenario)
        if unknown or missing:
            raise ValueError(
                f"render scenario #{index} fields invalid; unknown={sorted(unknown)}, missing={sorted(missing)}"
            )
        name = scenario["name"]
        if not isinstance(name, str) or not name:
            raise ValueError(f"render scenario #{index} has an invalid name")
        if name in names:
            raise ValueError(f"duplicate render scenario: {name}")
        names.add(name)
        if scenario["status"] not in {"regression", "developer", "documentation"}:
            raise ValueError(f"render scenario {name} has unknown status {scenario['status']!r}")
        platforms = scenario["platforms"]
        if not isinstance(platforms, list) or not platforms or set(platforms) - {"windows", "macos", "linux"}:
            raise ValueError(f"render scenario {name} has invalid platforms")
        for field in ("ctest", "reference_required", "renderall", "blessall"):
            if not isinstance(scenario[field], bool):
                raise ValueError(f"render scenario {name} field {field} must be boolean")
        for field in ("purpose", "compare_command", "bless_command"):
            if not isinstance(scenario[field], str):
                raise ValueError(f"render scenario {name} field {field} must be a string")
        if scenario["ctest"] and not scenario["reference_required"]:
            raise ValueError(f"CTest render scenario {name} must require references")
        if scenario["renderall"] != scenario["ctest"]:
            raise ValueError(f"renderall and CTest inventory differ for {name}")
        if scenario["blessall"] and not scenario["reference_required"]:
            raise ValueError(f"blessall scenario {name} must require references")
        for field in ("compare_command", "bless_command"):
            command = scenario[field]
            if command:
                if command in commands:
                    raise ValueError(f"duplicate render command: {command}")
                commands.add(command)

    if validate_files:
        render_dir = root / "tests" / "render"
        toml_names = {path.stem for path in render_dir.glob("*.toml")}
        missing_toml = names - toml_names
        orphan_toml = toml_names - names
        if missing_toml or orphan_toml:
            raise ValueError(
                f"render TOML inventory mismatch; missing={sorted(missing_toml)}, orphaned={sorted(orphan_toml)}"
            )

        required_references = {
            f"{scenario['name']}.{platform}.bmp"
            for scenario in scenarios
            if scenario["reference_required"]
            for platform in scenario["platforms"]
        }
        reference_dir = render_dir / "reference"
        actual_references = {path.name for path in reference_dir.glob("*.bmp")}
        missing_references = required_references - actual_references
        declared_reference_prefixes = {f"{name}." for name in names}
        orphan_references = {
            reference
            for reference in actual_references
            if not any(reference.startswith(prefix) for prefix in declared_reference_prefixes)
        }
        if missing_references or orphan_references:
            raise ValueError(
                "render reference inventory mismatch; "
                f"missing={sorted(missing_references)}, orphaned={sorted(orphan_references)}"
            )
    return scenarios


def render_command_map(root: pathlib.Path) -> dict[str, tuple[str, bool]]:
    result: dict[str, tuple[str, bool]] = {}
    for scenario in load_render_manifest(root):
        if scenario["compare_command"]:
            result[scenario["compare_command"]] = (scenario["name"], False)
        if scenario["bless_command"]:
            result[scenario["bless_command"]] = (scenario["name"], True)
    return result


def render_scenario_names(root: pathlib.Path, flag: str) -> list[str]:
    return [scenario["name"] for scenario in load_render_manifest(root) if scenario[flag]]


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


def cmd_score_shot_check(root: pathlib.Path) -> int:
    """Regression guard for kanban 74 (score-screenshot-size-hang).

    The score host used to hang under `--screenshot-size` and for uncompressed
    `.musicxml` sources (the readiness pump never saw the slicer-ready signal), so
    only the plain window-capture `.mxl` path worked. This runs the previously
    hanging path headless under a timeout and requires a byte-exact BMP at the
    requested size. A hang, a non-zero exit, or a wrong-sized file fails the check.
    """
    fixture = (root / "plugins" / "scoreview" / "tests" / "fixtures" / "musicxml"
               / "grieg-waltz-op-12-no-2.musicxml")
    if not fixture.exists():
        print(f"score-shot-check: FAILED — fixture missing: {fixture}")
        return 1

    rc, exe, env = build_shortcut_exe(root)
    if rc != 0 or exe is None:
        return 1

    width, height = 640, 900
    expected = width * height * 4 + 54  # RGBA pixels + 54-byte BMP header
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "score-shot-check.bmp"
        cmd = [str(exe), "--console", "--host", "score",
               "--command", "paged analysis unique",
               "--source", str(fixture),
               "--screenshot", str(out),
               "--screenshot-size", f"{width}x{height}"]
        try:
            proc = subprocess.run(cmd, cwd=root, env=env, timeout=90, check=False)
        except subprocess.TimeoutExpired:
            print("score-shot-check: FAILED — score host hung (kanban 74 regression)")
            return 1
        if proc.returncode != 0:
            print(f"score-shot-check: FAILED — exit {proc.returncode}")
            return 1
        if not out.exists():
            print("score-shot-check: FAILED — no screenshot written")
            return 1
        actual = out.stat().st_size
        if actual != expected:
            print(f"score-shot-check: FAILED — BMP is {actual} bytes, "
                  f"expected {expected} ({width}x{height} RGBA + header)")
            return 1

    print(f"score-shot-check: OK — {width}x{height} BMP from .musicxml, no hang")
    return 0


def ensure_built(root: pathlib.Path) -> int:
    exe = draxul_path(root)
    if exe.exists():
        return 0

    if sys.platform.startswith("win"):
        return run(["cmake", "--build", str(build_dir(root)), "--config", "Release", "--parallel", _parallel_jobs()], root)
    return run(["cmake", "--build", str(build_dir(root)), "--parallel", _parallel_jobs()], root)


def cmd_clean(root: pathlib.Path) -> int:
    directories = build_dirs(root)
    if not directories:
        print(f"Build directories already absent under: {root}")
        return 0

    for directory in directories:
        print(f"Removing build directory: {directory}")
        _remove_tree(directory)
    return 0


# --- Repository hygiene -----------------------------------------------------
#
# Artifacts that must never be tracked. OS/coverage temps are forbidden anywhere
# in the tree; log/object/bitmap files are forbidden only at the repo root, so
# legitimate assets keep working (mesh `*.obj` under module dirs, render-test
# reference `*.bmp` under tests/render/reference/).
FORBIDDEN_ANYWHERE_NAMES = frozenset({".DS_Store"})
FORBIDDEN_ANYWHERE_PREFIXES = (".!",)  # partial-transfer temps, e.g. ".!75583!.DS_Store"
FORBIDDEN_ANYWHERE_SUFFIXES = (".profraw", ".profdata")
FORBIDDEN_ROOT_NAMES = frozenset({"key.txt", "megacity-linux-drivers-mesh.bmp", "NUL.obj"})
FORBIDDEN_ROOT_SUFFIXES = (".log", ".obj", ".bmp")

# A canonical feature inventory lives at docs/features.md. Root FEATURES.md must
# stay a short pointer to it rather than growing into a second inventory.
FEATURE_DOC_POINTER_MAX_LINES = 40


def forbidden_artifacts(tracked_relpaths: list[str]) -> list[str]:
    """Return the tracked paths that violate the artifact policy (sorted, unique)."""
    offenders: set[str] = set()
    for raw in tracked_relpaths:
        norm = raw.replace("\\", "/").strip()
        if not norm:
            continue
        name = norm.rsplit("/", 1)[-1]
        at_root = "/" not in norm
        if (
            name in FORBIDDEN_ANYWHERE_NAMES
            or name.startswith(FORBIDDEN_ANYWHERE_PREFIXES)
            or name.endswith(FORBIDDEN_ANYWHERE_SUFFIXES)
            or (at_root and (name in FORBIDDEN_ROOT_NAMES or name.endswith(FORBIDDEN_ROOT_SUFFIXES)))
        ):
            offenders.add(norm)
    return sorted(offenders)


def feature_doc_problems(features_md: str | None, docs_features_exists: bool) -> list[str]:
    """Flag a missing canonical inventory or a FEATURES.md that duplicates it."""
    problems: list[str] = []
    if not docs_features_exists:
        problems.append("docs/features.md (the canonical feature inventory) is missing")
    if features_md is not None:
        if "docs/features.md" not in features_md:
            problems.append("FEATURES.md must point to docs/features.md (no reference found)")
        line_count = len(features_md.splitlines())
        if line_count > FEATURE_DOC_POINTER_MAX_LINES:
            problems.append(
                f"FEATURES.md has {line_count} lines (> {FEATURE_DOC_POINTER_MAX_LINES}); it should be a "
                "short pointer to docs/features.md, not a second feature inventory"
            )
    return problems


def tracked_files(root: pathlib.Path) -> list[str]:
    """Repo-relative paths git is tracking — the authoritative view for hygiene."""
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return []
    return [line for line in result.stdout.splitlines() if line.strip()]


def cmd_hygiene(root: pathlib.Path) -> int:
    """Fail if forbidden artifacts are tracked or the feature docs have duplicated."""
    problems: list[str] = []
    problems.extend(
        f"forbidden tracked artifact: {path}" for path in forbidden_artifacts(tracked_files(root))
    )
    features_path = root / "FEATURES.md"
    features_md = features_path.read_text(encoding="utf-8", errors="replace") if features_path.is_file() else None
    problems.extend(feature_doc_problems(features_md, (root / "docs" / "features.md").is_file()))

    if problems:
        print("Hygiene check FAILED:")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print("Hygiene check passed: no forbidden tracked artifacts; single feature-doc source of truth.")
    return 0


# --- Kanban report ----------------------------------------------------------

KANBAN_LANES = ("pending", "ice-box", "done")

_TASK_CHECKED_RE = re.compile(r"(?m)^\s*[-*]\s+\[[xX]\]\s")
_TASK_UNCHECKED_RE = re.compile(r"(?m)^\s*[-*]\s+\[ \]\s")


def count_task_boxes(text: str) -> tuple[int, int]:
    """Return (checked, unchecked) Markdown task-list box counts in ``text``."""
    return (len(_TASK_CHECKED_RE.findall(text)), len(_TASK_UNCHECKED_RE.findall(text)))


def lane_cards(lane_dir: pathlib.Path) -> list[pathlib.Path]:
    """Sorted Markdown cards in a kanban lane (README files excluded)."""
    if not lane_dir.is_dir():
        return []
    return sorted(
        path
        for path in lane_dir.glob("*.md")
        if path.is_file() and path.name.lower() != "readme.md"
    )


def cmd_kanban_report(root: pathlib.Path) -> int:
    """Summarize kanban lanes and flag ambiguous done cards. Never edits cards."""
    board = root / "kanban"
    if not board.is_dir():
        print(f"No kanban/ directory under {root}")
        return 0

    print("Kanban report (kanban/ is authoritative; this command never edits cards)")
    print()
    ambiguous: list[tuple[str, int, int]] = []
    ready: list[str] = []
    for lane in KANBAN_LANES:
        cards = lane_cards(board / lane)
        print(f"  {lane:<8} {len(cards)} cards")
        for card in cards:
            checked, unchecked = count_task_boxes(card.read_text(encoding="utf-8", errors="replace"))
            rel = card.relative_to(board).as_posix()
            if lane == "done" and unchecked > 0:
                ambiguous.append((rel, checked, unchecked))
            elif lane == "pending" and checked > 0 and unchecked == 0:
                ready.append(rel)

    print()
    if ambiguous:
        print(
            f"Ambiguous done cards ({len(ambiguous)}) — in kanban/done but still carrying unchecked "
            "boxes. Review by hand; this report never ticks or moves them:"
        )
        for rel, checked, unchecked in ambiguous:
            print(f"  - {rel}  [{checked} ticked / {unchecked} unchecked]")
    else:
        print("No ambiguous done cards: every kanban/done card is fully ticked.")

    if ready:
        print()
        print(f"Fully-ticked pending cards ({len(ready)}) — candidates to move to kanban/done:")
        for rel in ready:
            print(f"  - {rel}")
    return 0


def help_text() -> str:
    scenarios = load_render_manifest(repo_root())
    compare_help = "\n".join(
        f"  {scenario['compare_command']:<12} Run {scenario['name']} compare"
        for scenario in scenarios
        if scenario["compare_command"]
    )
    bless_help = "\n".join(
        f"  {scenario['bless_command']:<12} Bless {scenario['name']}"
        for scenario in scenarios
        if scenario["bless_command"]
    )
    return f"""Usage:
  do <command> [options]

Single-word shortcuts:
  build [debug|release|relwithdebinfo] [--reconfigure] [--vs|--ninja]
               Configure and build Draxul (default: debug, ninja on Windows)
  run [debug|release|relwithdebinfo] [--reconfigure] [--vs|--ninja] [--console]
      [--host megacity --parser treesitter|treesitter_db] [-- app-args...]
               Configure, build, and run Draxul
  deploy [release] [--reconfigure] [--vs|--ninja]
               Build Release and package deploy/YYYY_MM_DD/mac|win plus a zip archive
  clean        Remove repository build directories
  smoke        Run the app smoke test
  score-shot-check  Regression guard (kanban 74): score host --screenshot-size + .musicxml
  test         Run unit tests (four C++ shards plus do.py tests)
  shot         Regenerate the README hero screenshot
  api          Build local Doxygen API docs
  docs         Build all docs artifacts
  coverage     macOS: build with LLVM coverage, export build/coverage.lcov, copy to db/coverage.lcov
  syncboard    Sync kanban pending and ice-box to the GitHub project board
  hygiene      Check for forbidden tracked artifacts and duplicate feature docs
  kanban-report  Summarize kanban lanes; flag done cards with unchecked boxes

Deterministic render snapshots:
{compare_help}
  renderall    Run all compare snapshots

Bless render references:
{bless_help}
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
  do clean
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

    if command == "clean":
        if args[1:]:
            print(
                f"ERROR: clean does not accept arguments: {shlex.join(args[1:])}",
                file=sys.stderr,
            )
            return 2
        return cmd_clean(root)

    if command == "hygiene":
        if args[1:]:
            print(
                f"ERROR: hygiene does not accept arguments: {shlex.join(args[1:])}",
                file=sys.stderr,
            )
            return 2
        return cmd_hygiene(root)

    if command == "kanban-report":
        if args[1:]:
            print(
                f"ERROR: kanban-report does not accept arguments: {shlex.join(args[1:])}",
                file=sys.stderr,
            )
            return 2
        return cmd_kanban_report(root)

    if command == "test":
        if sys.platform.startswith("win"):
            return run(["cmd", "/c", "t.bat", "--unit"], root)
        return run(["sh", "./scripts/run_tests.sh", "--unit"], root)

    if command == "shot":
        cmd = [sys.executable, str(root / "scripts" / "update_screenshot.py")]
        if skip_build:
            cmd.append("--skip-build")
        return run(cmd, root)

    if command == "api":
        return run([sys.executable, str(root / "scripts" / "build_docs.py"), "--api-only"], root)

    if command == "docs":
        return run([sys.executable, str(root / "scripts" / "build_docs.py")], root)

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
        print(f"> ctest --test-dir {bd} --label-regex unit --parallel 4 --output-on-failure")
        rc = subprocess.run(
            [
                "ctest", "--test-dir", str(bd),
                "--label-regex", "unit", "--parallel", "4", "--output-on-failure",
            ],
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
        # 5. Export LCOV from every focused test executable. llvm-cov accepts
        # additional instrumented objects via -object, so the modular test
        # targets retain one combined report just like the former monolith.
        lcov_file = bd / "coverage.lcov"
        test_dir = bd / "tests"
        test_executables = [
            test_dir / name
            for name in (
                "draxul-test-core",
                "draxul-test-app",
                "draxul-test-markdown-kanban",
                "draxul-test-megacity",
                "draxul-test-satview",
                "draxul-test-scoreview",
                "draxul-test-scoreview-host",
            )
            if (test_dir / name).is_file()
        ]
        if not test_executables:
            print("ERROR: no modular test executables found")
            return 1
        cov_command = ["xcrun", "llvm-cov", "export", str(test_executables[0])]
        for test_exe in test_executables[1:]:
            cov_command.extend(["-object", str(test_exe)])
        cov_command.extend([
            f"--instr-profile={profdata}",
            "--format=lcov",
            "--ignore-filename-regex=(build/_deps|tests/)",
        ])
        rc = subprocess.run(
            cov_command,
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

    if command == "score-shot-check":
        return cmd_score_shot_check(root)

    render_map = render_command_map(root)

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
        for scenario_name in render_scenario_names(root, "renderall"):
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
        for scenario_name in render_scenario_names(root, "blessall"):
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
