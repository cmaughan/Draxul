"""Shared helpers for the external plugin SDK smoke drivers.

Consumed by tests/external_plugin_sdk_smoke.py (spinning triangle) and
plugins/scoreview/tests/external_product_plugin_smoke.py (ScoreView). Both
gates get the same argument surface, SDK install + ABI-leak check, external
configure/build wrappers, isolated app staging, plugin-load assertion, BMP
validation, and — on render failure — the child process output dump.
(Consolidates the two historical ~230-line drivers; audit bug #13.)
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys


def run(command: list[str], *, env: dict[str, str] | None = None,
        cwd: pathlib.Path | None = None, timeout: int | None = None) -> None:
    """Echo and run a command, raising on a non-zero exit code."""
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True, env=env, cwd=cwd, timeout=timeout)


def make_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--build-root", type=pathlib.Path, required=True)
    parser.add_argument("--draxul", type=pathlib.Path, required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--platform", default="")
    parser.add_argument("--toolset", default="")
    parser.add_argument("--c-compiler", default="")
    parser.add_argument("--cxx-compiler", default="")
    parser.add_argument("--make-program", default="")
    parser.add_argument("--toolchain-file", default="")
    parser.add_argument("--render", action="store_true")
    return parser


def find_one(root: pathlib.Path, name: str) -> pathlib.Path:
    matches = list(root.rglob(name))
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name}, found {matches}")
    return matches[0]


def shared_module_name(stem: str) -> str:
    if sys.platform.startswith("win"):
        return f"{stem}.dll"
    if sys.platform == "darwin":
        return f"{stem}.dylib"
    return f"{stem}.so"


def find_shared_module(root: pathlib.Path, stem: str) -> pathlib.Path:
    """Locate a built plugin module by stem, tolerating CMake's default
    MODULE suffix on macOS (.so) alongside the canonical .dylib. Callers
    stage the result under shared_module_name(stem) so the plugin manifest's
    platform library name always matches."""
    if sys.platform.startswith("win"):
        suffixes = [".dll"]
    elif sys.platform == "darwin":
        suffixes = [".dylib", ".so"]
    else:
        suffixes = [".so"]
    matches = [match for suffix in suffixes
               for match in root.rglob(stem + suffix)]
    if len(matches) != 1:
        raise RuntimeError(
            f"expected one {stem} module ({suffixes}), found {matches}")
    return matches[0]


def install_sdk(args: argparse.Namespace, sdk_prefix: pathlib.Path) -> None:
    """Install the draxul-plugin-sdk component and verify the public header
    does not leak private rendering ABI (both smoke gates run this check)."""
    run([
        args.cmake,
        "--install", str(args.build_root),
        "--prefix", str(sdk_prefix),
        "--component", "draxul-plugin-sdk",
        "--config", args.config,
    ])
    public_header = (sdk_prefix / "include" / "draxul"
                     / "plugin_api.h").read_text(encoding="utf-8")
    forbidden_private_abi = ("ImGui", "Canvas2D", "IRenderPass",
                             "cpp_abi_fingerprint")
    leaked = [token for token in forbidden_private_abi
              if token in public_header]
    if leaked:
        raise RuntimeError(
            f"installed SDK leaks private rendering ABI: {leaked}")


def configure_external(args: argparse.Namespace, source: pathlib.Path,
                       build_dir: pathlib.Path, sdk_prefix: pathlib.Path,
                       *, timeout: int | None = None) -> None:
    configure = [
        args.cmake,
        "-S", str(source),
        "-B", str(build_dir),
        "-G", args.generator,
        f"-DCMAKE_PREFIX_PATH={sdk_prefix}",
    ]
    single_config = ("Visual Studio" not in args.generator
                     and "Xcode" not in args.generator
                     and "Multi-Config" not in args.generator)
    if single_config:
        configure.append(f"-DCMAKE_BUILD_TYPE={args.config}")
        inherited_cache_variables = (
            ("CMAKE_C_COMPILER", args.c_compiler),
            ("CMAKE_CXX_COMPILER", args.cxx_compiler),
            ("CMAKE_MAKE_PROGRAM", args.make_program),
        )
        configure.extend(
            f"-D{name}={value}"
            for name, value in inherited_cache_variables
            if value
        )
    if args.toolchain_file:
        configure.append(
            f"-DCMAKE_TOOLCHAIN_FILE={args.toolchain_file}")
    if args.platform:
        configure.extend(["-A", args.platform])
    if args.toolset:
        configure.extend(["-T", args.toolset])
    run(configure, timeout=timeout)


def build_external(args: argparse.Namespace, build_dir: pathlib.Path,
                   target: str, *, parallel: int | None = None,
                   timeout: int | None = None) -> None:
    command = [
        args.cmake,
        "--build", str(build_dir),
        "--config", args.config,
        "--target", target,
    ]
    if parallel is not None:
        command.extend(["--parallel", str(parallel)])
    run(command, timeout=timeout)


def stage_app_layout(temp: pathlib.Path, draxul: pathlib.Path,
                     plugin_id: str) -> tuple[pathlib.Path, pathlib.Path]:
    """Create the isolated executable + bundled-plugin directories and copy
    the Draxul binary in. Returns (executable_dir, plugin_dir)."""
    if sys.platform == "darwin":
        app_root = temp / "Draxul.app"
        executable_dir = app_root / "Contents" / "MacOS"
        plugin_dir = app_root / "Contents" / "PlugIns" / plugin_id
    else:
        executable_dir = temp / "app"
        plugin_dir = executable_dir / "plugins" / plugin_id
    executable_dir.mkdir(parents=True)
    plugin_dir.mkdir(parents=True)
    shutil.copy2(draxul, executable_dir / draxul.name)
    return executable_dir, plugin_dir


def isolated_env(temp: pathlib.Path) -> dict[str, str]:
    env = os.environ.copy()
    env["APPDATA"] = str(temp / "user-appdata")
    env["HOME"] = str(temp / "home")
    return env


def user_plugin_dir(env: dict[str, str], plugin_id: str) -> pathlib.Path:
    if sys.platform == "darwin":
        return (pathlib.Path(env["HOME"]) / "Library"
                / "Application Support" / "draxul" / "plugins" / plugin_id)
    if sys.platform.startswith("win"):
        return pathlib.Path(env["APPDATA"]) / "draxul" / "plugins" / plugin_id
    return (pathlib.Path(env["HOME"]) / ".local" / "share" / "draxul"
            / "plugins" / plugin_id)


def assert_plugin_loads(draxul: pathlib.Path, plugin_id: str,
                        expected_library: pathlib.Path,
                        env: dict[str, str],
                        *, timeout: int | None = 30) -> dict:
    """Query `draxul plugin get <id> --json` and assert identity, availability,
    and that the loaded library is the expected isolated copy."""
    result = subprocess.run(
        [str(draxul), "plugin", "get", plugin_id, "--json"],
        check=True, capture_output=True, text=True, env=env, timeout=timeout)
    metadata = json.loads(result.stdout)
    if not metadata.get("available"):
        raise RuntimeError(f"external plugin did not load: {metadata}")
    if metadata.get("id") != plugin_id:
        raise RuntimeError(f"external plugin identity mismatch: {metadata}")
    loaded_library = pathlib.Path(metadata["library"]).resolve()
    if loaded_library != expected_library.resolve():
        raise RuntimeError(
            f"Draxul loaded {loaded_library}, not isolated {expected_library}")
    print(json.dumps(metadata, indent=2), flush=True)
    return metadata


def run_render(draxul: pathlib.Path, scenario: pathlib.Path,
               rendered: pathlib.Path, *, env: dict[str, str],
               cwd: pathlib.Path, timeout: int = 90,
               what: str = "external render") -> None:
    """Run a --render-test export; on failure dump the child's stdout/stderr
    (both smoke gates get the diagnostic dump)."""
    command = [str(draxul)]
    if sys.platform.startswith("win"):
        command.append("--console")
    command.extend([
        "--render-test", str(scenario),
        "--export-render-test", str(rendered),
        # Keep host-side diagnostics in the captured stderr so a failing
        # staged render explains itself. Output is only printed on failure.
        "--log-level", "debug",
    ])
    print("+", subprocess.list2cmdline(command), flush=True)
    process = subprocess.run(command, capture_output=True, text=True,
                             env=env, cwd=cwd, timeout=timeout)
    if process.returncode != 0:
        if process.stdout:
            print(process.stdout, flush=True)
        if process.stderr:
            print(process.stderr, file=sys.stderr, flush=True)
        raise RuntimeError(
            f"{what} failed with exit code {process.returncode}")


def validate_rendered_bmp(path: pathlib.Path, *, min_colors: int,
                          what: str = "external render") -> None:
    contents = path.read_bytes()
    if len(contents) < 54 or contents[:2] != b"BM":
        raise RuntimeError(f"{what} did not produce a BMP")
    pixel_offset = struct.unpack_from("<I", contents, 10)[0]
    width, height = struct.unpack_from("<ii", contents, 18)
    bits_per_pixel = struct.unpack_from("<H", contents, 28)[0]
    if (width, abs(height), bits_per_pixel) != (960, 640, 32):
        raise RuntimeError(
            f"unexpected {what} frame {width}x{height}x{bits_per_pixel}")
    pixels = contents[pixel_offset:]
    colors = {pixels[index:index + 4] for index in range(0, len(pixels), 4)}
    if len(colors) < min_colors:
        raise RuntimeError(
            f"{what} frame is effectively blank ({len(colors)} colors)")
