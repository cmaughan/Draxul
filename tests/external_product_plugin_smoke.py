"""Build ScoreView from a copied product tree using only the installed SDK."""

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


PLUGIN_ID = "dev.draxul.scoreview"


def run(command: list[str], *, timeout: int | None = None) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True, timeout=timeout)


def find_one(root: pathlib.Path, name: str) -> pathlib.Path:
    matches = list(root.rglob(name))
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name}, found {matches}")
    return matches[0]


def module_name() -> str:
    if sys.platform.startswith("win"):
        return "draxul-scoreview.dll"
    if sys.platform == "darwin":
        return "draxul-scoreview.dylib"
    return "draxul-scoreview.so"


def verovio_name() -> str:
    if sys.platform.startswith("win"):
        return "verovio.dll"
    if sys.platform == "darwin":
        return "libverovio.dylib"
    return "libverovio.so"


def validate_rendered_bmp(path: pathlib.Path) -> None:
    contents = path.read_bytes()
    if len(contents) < 54 or contents[:2] != b"BM":
        raise RuntimeError("extracted ScoreView render did not produce a BMP")
    pixel_offset = struct.unpack_from("<I", contents, 10)[0]
    width, height = struct.unpack_from("<ii", contents, 18)
    bits_per_pixel = struct.unpack_from("<H", contents, 28)[0]
    if (width, abs(height), bits_per_pixel) != (960, 640, 32):
        raise RuntimeError(
            f"unexpected ScoreView frame {width}x{height}x{bits_per_pixel}")
    pixels = contents[pixel_offset:]
    colors = {pixels[index:index + 4]
              for index in range(0, len(pixels), 4)}
    if len(colors) < 16:
        raise RuntimeError(
            f"extracted ScoreView frame is effectively blank ({len(colors)} colors)")


def copy_draxul_runtime_assets(draxul: pathlib.Path,
                               executable_dir: pathlib.Path) -> None:
    if sys.platform == "darwin":
        source_resources = draxul.parent.parent / "Resources"
        destination = executable_dir.parent / "Resources"
        if source_resources.is_dir():
            shutil.copytree(source_resources, destination)
        return
    for name in ("assets", "fonts", "shaders"):
        source = draxul.parent / name
        if source.is_dir():
            shutil.copytree(source, executable_dir / name)


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

    source_root = args.source_root.resolve()
    # Keeping the isolated checkout beside, rather than beneath, the repository
    # makes accidental app/libs/modules access visible and avoids MSBuild's
    # restrictions on the system temporary directory.
    with tempfile.TemporaryDirectory(
            prefix=".draxul-product-extraction-",
            dir=source_root.parent) as raw_temp:
        temp = pathlib.Path(raw_temp)
        sdk_prefix = temp / "sdk"
        copied_plugins = temp / "plugins"
        source = copied_plugins / "scoreview"
        external_build = temp / "build"

        run([
            args.cmake, "--install", str(args.build_root),
            "--prefix", str(sdk_prefix),
            "--component", "draxul-plugin-sdk",
            "--config", args.config,
        ])
        shutil.copytree(source_root / "plugins" / "scoreview", source)
        shutil.copytree(source_root / "plugins" / "support" / "imgui",
                        copied_plugins / "support" / "imgui")

        forbidden_roots = [temp / name for name in ("app", "libs", "modules")]
        if any(path.exists() for path in forbidden_roots):
            raise RuntimeError("isolated product tree contains a Draxul core tree")

        configure = [
            args.cmake, "-S", str(source), "-B", str(external_build),
            "-G", args.generator,
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
        run(configure, timeout=600)
        run([
            args.cmake, "--build", str(external_build),
            "--config", args.config,
            "--target", "draxul-scoreview-plugin",
            # Verovio presents hundreds of translation units to MSVC in one
            # target. Bound parallelism so this acceptance test remains usable
            # on developer workstations instead of saturating input/UI work.
            "--parallel", str(min(12, max(2, os.cpu_count() or 2))),
        ], timeout=2400)

        module = find_one(external_build, module_name())
        if sys.platform == "darwin":
            executable_dir = temp / "Draxul.app" / "Contents" / "MacOS"
            plugin_dir = (temp / "Draxul.app" / "Contents" / "PlugIns"
                          / PLUGIN_ID)
        else:
            executable_dir = temp / "app"
            plugin_dir = executable_dir / "plugins" / PLUGIN_ID
        executable_dir.mkdir(parents=True)
        plugin_dir.mkdir(parents=True)
        clean_draxul = executable_dir / args.draxul.name
        shutil.copy2(args.draxul, clean_draxul)
        copy_draxul_runtime_assets(args.draxul, executable_dir)
        shutil.copy2(source / "plugin.toml", plugin_dir / "plugin.toml")
        shutil.copy2(module, plugin_dir / module.name)
        verovio = find_one(external_build, verovio_name())
        shutil.copy2(verovio, plugin_dir / verovio.name)
        verovio_source = external_build / "_deps" / "verovio-src"
        shutil.copytree(verovio_source / "data", plugin_dir / "verovio-data")
        shutil.copy2(verovio_source / "fonts" / "Leipzig" / "Leipzig.ttf",
                     plugin_dir / "verovio-data" / "Leipzig.ttf")
        soundfont_source = external_build / "_deps" / "ydp_grand_piano-src"
        shutil.copytree(soundfont_source, plugin_dir / "soundfonts")
        shader_dir = plugin_dir / "shaders"
        shader_dir.mkdir()
        if sys.platform.startswith("win"):
            for name in ("nanovg.vert.spv", "nanovg.frag.spv"):
                shutil.copy2(find_one(external_build, name), shader_dir / name)

        env = os.environ.copy()
        env["APPDATA"] = str(temp / "user-appdata")
        env["HOME"] = str(temp / "home")
        result = subprocess.run(
            [str(clean_draxul), "plugin", "get", PLUGIN_ID, "--json"],
            check=True, capture_output=True, text=True, env=env, timeout=30)
        metadata = json.loads(result.stdout)
        if not metadata.get("available") or metadata.get("id") != PLUGIN_ID:
            raise RuntimeError(f"copied ScoreView did not load: {metadata}")
        loaded = pathlib.Path(metadata["library"]).resolve()
        if loaded != (plugin_dir / module.name).resolve():
            raise RuntimeError(
                f"Draxul loaded {loaded}, not extracted {plugin_dir / module.name}")
        print(json.dumps(metadata, indent=2), flush=True)

        if args.render:
            fixture_dir = plugin_dir / "fixtures"
            fixture_dir.mkdir()
            fixture = fixture_dir / "grieg.musicxml"
            shutil.copy2(source / "tests" / "fixtures" / "musicxml"
                         / "grieg-waltz-op-12-no-2.musicxml", fixture)
            scenario = temp / "scoreview-extracted.toml"
            config_json = json.dumps({
                "source": str(fixture),
                "mode": "paged",
            })
            scenario.write_text(
                "\n".join([
                    'name = "scoreview-extracted"',
                    'host = "plugin"',
                    f'plugin_id = "{PLUGIN_ID}"',
                    f"plugin_config_json = '{config_json}'",
                    "width = 960",
                    "height = 640",
                    "settle_ms = 1200",
                    'commands = [""]',
                    "",
                ]), encoding="utf-8")
            rendered = temp / "scoreview-extracted.bmp"
            render_command = [str(clean_draxul)]
            if sys.platform.startswith("win"):
                render_command.append("--console")
            render_command.extend([
                "--render-test", str(scenario),
                "--export-render-test", str(rendered),
            ])
            print("+", subprocess.list2cmdline(render_command), flush=True)
            rendered_process = subprocess.run(
                render_command, capture_output=True, text=True, env=env,
                cwd=executable_dir, timeout=90)
            if rendered_process.returncode != 0:
                if rendered_process.stdout:
                    print(rendered_process.stdout, flush=True)
                if rendered_process.stderr:
                    print(rendered_process.stderr, file=sys.stderr, flush=True)
                raise RuntimeError(
                    "extracted ScoreView render failed with exit code "
                    f"{rendered_process.returncode}")
            validate_rendered_bmp(rendered)
            print("Extracted ScoreView Grieg render: PASS", flush=True)

        print("ScoreView extraction boundary: PASS", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
