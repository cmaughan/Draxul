"""Build the triangle as an external SDK consumer and load it in Draxul."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


def run(command: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True, env=env)


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

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
