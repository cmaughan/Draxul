"""Build the triangle as an external SDK consumer and load it in Draxul."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile


def run(command: list[str], *, env: dict[str, str] | None = None,
        cwd: pathlib.Path | None = None, timeout: int | None = None) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True, env=env, cwd=cwd, timeout=timeout)


def find_module(build_dir: pathlib.Path) -> pathlib.Path:
    if sys.platform.startswith("win"):
        name = "draxul-spinning-triangle.dll"
    elif sys.platform == "darwin":
        name = "draxul-spinning-triangle.dylib"
    else:
        name = "draxul-spinning-triangle.so"
    matches = list(build_dir.rglob(name))
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name}, found {matches}")
    return matches[0]


def copy_render_assets(build_dir: pathlib.Path,
                       plugin_dir: pathlib.Path) -> None:
    names = (["spinning_triangle.metallib"] if sys.platform == "darwin" else
             ["spinning_triangle.vert.spv", "spinning_triangle.frag.spv"])
    for name in names:
        matches = list(build_dir.rglob(name))
        if len(matches) != 1:
            raise RuntimeError(f"expected one external {name}, found {matches}")
        shutil.copy2(matches[0], plugin_dir / name)


def validate_rendered_bmp(path: pathlib.Path) -> None:
    contents = path.read_bytes()
    if len(contents) < 54 or contents[:2] != b"BM":
        raise RuntimeError("external triangle render did not produce a BMP")
    pixel_offset = struct.unpack_from("<I", contents, 10)[0]
    width, height = struct.unpack_from("<ii", contents, 18)
    bits_per_pixel = struct.unpack_from("<H", contents, 28)[0]
    if (width, abs(height), bits_per_pixel) != (960, 640, 32):
        raise RuntimeError(
            f"unexpected external triangle frame {width}x{height}x{bits_per_pixel}")
    pixels = contents[pixel_offset:]
    colors = {pixels[index:index + 4] for index in range(0, len(pixels), 4)}
    if len(colors) < 8:
        raise RuntimeError(
            f"external triangle frame is effectively blank ({len(colors)} colors)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    parser.add_argument("--build-root", type=pathlib.Path, required=True)
    parser.add_argument("--draxul", type=pathlib.Path, required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--platform", default="")
    parser.add_argument("--toolset", default="")
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()

    # Keep the isolated tree outside both the source checkout and Windows'
    # system Temp directory (MSBuild warns that the latter is not safe for
    # incremental compiler outputs).
    with tempfile.TemporaryDirectory(
            prefix=".draxul-sdk-smoke-",
            dir=args.source_root.resolve().parent) as raw_temp:
        temp = pathlib.Path(raw_temp)
        sdk_prefix = temp / "sdk"
        source = temp / "spinning-triangle"
        external_build = temp / "build"

        run([
            args.cmake,
            "--install",
            str(args.build_root),
            "--prefix",
            str(sdk_prefix),
            "--component",
            "draxul-plugin-sdk",
            "--config",
            args.config,
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
        shutil.copytree(args.source_root / "plugins" / "spinning-triangle", source)

        configure = [
            args.cmake,
            "-S",
            str(source),
            "-B",
            str(external_build),
            "-G",
            args.generator,
            f"-DCMAKE_PREFIX_PATH={sdk_prefix}",
        ]
        if ("Visual Studio" not in args.generator
                and "Xcode" not in args.generator
                and "Multi-Config" not in args.generator):
            configure.append(f"-DCMAKE_BUILD_TYPE={args.config}")
        if args.platform:
            configure.extend(["-A", args.platform])
        if args.toolset:
            configure.extend(["-T", args.toolset])
        run(configure)
        run([
            args.cmake,
            "--build",
            str(external_build),
            "--config",
            args.config,
            "--target",
            "draxul-spinning-triangle",
        ])

        module = find_module(external_build)
        if sys.platform == "darwin":
            app_root = temp / "Draxul.app"
            executable_dir = app_root / "Contents" / "MacOS"
            plugin_dir = (app_root / "Contents" / "PlugIns"
                          / "dev.draxul.spinning-triangle")
        else:
            executable_dir = temp / "app"
            plugin_dir = (executable_dir / "plugins"
                          / "dev.draxul.spinning-triangle")
        executable_dir.mkdir(parents=True)
        plugin_dir.mkdir(parents=True)
        clean_draxul = executable_dir / args.draxul.name
        shutil.copy2(args.draxul, clean_draxul)
        shutil.copy2(source / "plugin.toml", plugin_dir / "plugin.toml")
        shutil.copy2(module, plugin_dir / module.name)
        copy_render_assets(external_build, plugin_dir)

        env = os.environ.copy()
        env["APPDATA"] = str(temp / "user-appdata")
        env["HOME"] = str(temp / "home")
        result = subprocess.run(
            [str(clean_draxul), "plugin", "get",
             "dev.draxul.spinning-triangle", "--json"],
            check=True,
            capture_output=True,
            text=True,
            env=env,
        )
        metadata = json.loads(result.stdout)
        if not metadata.get("available"):
            raise RuntimeError(f"external plugin did not load: {metadata}")
        if metadata.get("id") != "dev.draxul.spinning-triangle":
            raise RuntimeError(f"external plugin identity mismatch: {metadata}")
        loaded_library = pathlib.Path(metadata["library"]).resolve()
        if loaded_library != (plugin_dir / module.name).resolve():
            raise RuntimeError(
                f"Draxul loaded {loaded_library}, not isolated {plugin_dir / module.name}")
        print(json.dumps(metadata, indent=2), flush=True)

        if args.render:
            if sys.platform == "darwin":
                user_plugin_dir = (pathlib.Path(env["HOME"]) / "Library"
                                   / "Application Support" / "draxul"
                                   / "plugins"
                                   / "dev.draxul.spinning-triangle")
            elif sys.platform.startswith("win"):
                user_plugin_dir = (pathlib.Path(env["APPDATA"]) / "draxul"
                                   / "plugins"
                                   / "dev.draxul.spinning-triangle")
            else:
                user_plugin_dir = (pathlib.Path(env["HOME"]) / ".local"
                                   / "share" / "draxul" / "plugins"
                                   / "dev.draxul.spinning-triangle")
            shutil.copytree(plugin_dir, user_plugin_dir)

            user_query = subprocess.run(
                [str(args.draxul), "plugin", "get",
                 "dev.draxul.spinning-triangle", "--json"],
                check=True, capture_output=True, text=True, env=env)
            user_metadata = json.loads(user_query.stdout)
            if pathlib.Path(user_metadata["library"]).resolve() != (
                    user_plugin_dir / module.name).resolve():
                raise RuntimeError(
                    f"render probe did not select external plugin: {user_metadata}")

            scenario_dir = temp / "render"
            scenario_dir.mkdir()
            scenario = scenario_dir / "spinning-triangle.toml"
            shutil.copy2(args.source_root / "tests" / "render"
                         / "spinning-triangle.toml", scenario)
            rendered = scenario_dir / "external-triangle.bmp"
            render_command = [str(args.draxul)]
            if sys.platform.startswith("win"):
                render_command.append("--console")
            render_command.extend([
                "--render-test", str(scenario),
                "--export-render-test", str(rendered),
            ])
            run(render_command, env=env, cwd=args.draxul.parent, timeout=90)
            validate_rendered_bmp(rendered)
            print(f"external raw-GPU render: {rendered} (960x640, non-blank)",
                  flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
