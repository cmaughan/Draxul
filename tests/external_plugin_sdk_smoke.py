"""Build the triangle as an external SDK consumer and load it in Draxul."""

from __future__ import annotations

import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "support"))

import sdk_smoke  # noqa: E402

PLUGIN_ID = "dev.draxul.spinning-triangle"


def copy_render_assets(build_dir: pathlib.Path,
                       plugin_dir: pathlib.Path) -> None:
    names = (["spinning_triangle.metallib"] if sys.platform == "darwin" else
             ["spinning_triangle.vert.spv", "spinning_triangle.frag.spv"])
    for name in names:
        shutil.copy2(sdk_smoke.find_one(build_dir, name), plugin_dir / name)


def main() -> int:
    args = sdk_smoke.make_argument_parser().parse_args()

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

        sdk_smoke.install_sdk(args, sdk_prefix)
        shutil.copytree(args.source_root / "plugins" / "spinning-triangle",
                        source)

        sdk_smoke.configure_external(args, source, external_build, sdk_prefix)
        sdk_smoke.build_external(args, external_build,
                                 "draxul-spinning-triangle")

        module = sdk_smoke.find_one(
            external_build, sdk_smoke.shared_module_name(
                "draxul-spinning-triangle"))
        executable_dir, plugin_dir = sdk_smoke.stage_app_layout(
            temp, args.draxul, PLUGIN_ID)
        clean_draxul = executable_dir / args.draxul.name
        shutil.copy2(source / "plugin.toml", plugin_dir / "plugin.toml")
        shutil.copy2(module, plugin_dir / module.name)
        copy_render_assets(external_build, plugin_dir)

        env = sdk_smoke.isolated_env(temp)
        sdk_smoke.assert_plugin_loads(
            clean_draxul, PLUGIN_ID, plugin_dir / module.name, env,
            timeout=None)

        if args.render:
            user_dir = sdk_smoke.user_plugin_dir(env, PLUGIN_ID)
            shutil.copytree(plugin_dir, user_dir)
            sdk_smoke.assert_plugin_loads(
                args.draxul, PLUGIN_ID, user_dir / module.name, env,
                timeout=None)

            scenario_dir = temp / "render"
            scenario_dir.mkdir()
            scenario = scenario_dir / "spinning-triangle.toml"
            shutil.copy2(args.source_root / "tests" / "render"
                         / "spinning-triangle.toml", scenario)
            rendered = scenario_dir / "external-triangle.bmp"
            sdk_smoke.run_render(args.draxul, scenario, rendered, env=env,
                                 cwd=args.draxul.parent, timeout=90,
                                 what="external triangle render")
            sdk_smoke.validate_rendered_bmp(
                rendered, min_colors=8, what="external triangle render")
            print(f"external raw-GPU render: {rendered} (960x640, non-blank)",
                  flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
