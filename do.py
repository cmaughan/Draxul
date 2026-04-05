#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent


def build_dir(root: pathlib.Path) -> pathlib.Path:
    return root / "build"


def draxul_exe(bd: pathlib.Path, config: str) -> pathlib.Path:
    """Return the expected executable path for a given build dir and config."""
    if sys.platform.startswith("win"):
        return bd / config / "draxul.exe"
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
            preset = "win-ninja-debug" if mode == "debug" else "win-ninja-release"
            bd = root / "build-ninja"
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
    rc, bd, config, env = _configure_and_build(root, mode, force_reconfigure, build_system)
    if rc != 0:
        return rc

    exe = draxul_exe(bd, config)
    if not exe.exists():
        print(f"\nMissing executable: {exe}")
        return 1

    is_win = sys.platform.startswith("win")
    cmd: list[str] = [str(exe)] + app_args
    if is_win and not use_console:
        print(f"\n> start /wait {' '.join(cmd)}")
        proc = subprocess.run(["cmd", "/c", "start", "", "/wait"] + cmd, cwd=root, check=False, env=env)
        return proc.returncode
    else:
        return run(cmd, root, env=env)


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
  run [debug|release|relwithdebinfo] [--reconfigure] [--vs|--ninja] [--console] [-- app-args...]
               Configure, build, and run Draxul
  smoke        Run the app smoke test
  test         Run the full local test suite (t.bat / run_tests.sh)
  shot         Regenerate the README hero screenshot
  api          Build local Doxygen API docs
  docs         Build all docs artifacts
  review       Run AI code review (GPT + Claude; Gemini added on macOS), then
               synthesise a consensus — all in one shot
  review-bugs  Run bug-focused AI review (GPT + Claude; Gemini on macOS), then
               synthesise a bug triage consensus — all in one shot
  review-gemini  Run only the Gemini reviewer
  review-claude  Run only the Claude reviewer
  review-gpt     Run only the GPT reviewer
  consensus [claude|gpt|gemini]
               Run consensus synthesis on the latest reviews (default: claude)
  consensus-bugs [claude|gpt|gemini]
               Run bug triage consensus on the latest bug reviews (default: claude)
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
        rc = run([sys.executable, str(root / "scripts" / "do_review.py"), *args[1:]], root)
        if rc != 0:
            return rc
        prompt_file = root / "plans" / "prompts" / "consensus_review.md"
        output_file = root / "plans" / "reviews" / "review-consensus.md"
        return run([
            sys.executable,
            str(root / "scripts" / "ask_agent_claude.py"),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            "--full-auto",
        ], root)

    if command == "review-bugs":
        rc = run([sys.executable, str(root / "scripts" / "do_review_bugs.py"), *args[1:]], root)
        if rc != 0:
            return rc
        prompt_file = root / "plans" / "prompts" / "consensus_review_bugs.md"
        output_file = root / "plans" / "reviews" / "review-bugs-consensus.md"
        return run([
            sys.executable,
            str(root / "scripts" / "ask_agent_claude.py"),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            "--full-auto",
        ], root)

    if command in {"review-gemini", "review-claude", "review-gpt"}:
        agent = command.split("-", 1)[1]
        script_map = {
            "gemini": ("ask_agent_gemini.py", "review-latest.gemini.md", ["--full-auto"]),
            "claude": ("ask_agent_claude.py", "review-latest.claude.md", ["--full-auto"]),
            "gpt":    ("ask_agent_gpt.py",    "review-latest.gpt.md",    ["--review-safe"]),
        }
        script_name, output_name, extra_flags = script_map[agent]
        prompt_file = root / "plans" / "prompts" / "review.md"
        output_file = root / "plans" / "reviews" / output_name
        cmd = [
            sys.executable,
            str(root / "scripts" / script_name),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            *extra_flags,
            *args[1:],
        ]
        return run(cmd, root)

    if command == "consensus":
        agent_scripts = {
            "claude": "ask_agent_claude.py",
            "gpt": "ask_agent_gpt.py",
            "gemini": "ask_agent_gemini.py",
        }
        extra = args[1:]
        agent = "claude"
        if extra and extra[0] in agent_scripts:
            agent = extra[0]
            extra = extra[1:]
        prompt_file = root / "plans" / "prompts" / "consensus_review.md"
        output_file = root / "plans" / "reviews" / "review-consensus.md"
        cmd = [
            sys.executable,
            str(root / "scripts" / agent_scripts[agent]),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            "--full-auto",
            *extra,
        ]
        return run(cmd, root)

    if command == "consensus-bugs":
        agent_scripts = {
            "claude": "ask_agent_claude.py",
            "gpt": "ask_agent_gpt.py",
            "gemini": "ask_agent_gemini.py",
        }
        extra = args[1:]
        agent = "claude"
        if extra and extra[0] in agent_scripts:
            agent = extra[0]
            extra = extra[1:]
        prompt_file = root / "plans" / "prompts" / "consensus_review_bugs.md"
        output_file = root / "plans" / "reviews" / "review-bugs-consensus.md"
        cmd = [
            sys.executable,
            str(root / "scripts" / agent_scripts[agent]),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            "--full-auto",
            *extra,
        ]
        return run(cmd, root)

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
