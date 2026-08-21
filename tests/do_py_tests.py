from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_publish_plugin_module():
    spec = importlib.util.spec_from_file_location(
        "draxul_publish_plugin", ROOT / "tools" / "publish_plugin.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load tools/publish_plugin.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


publish_plugin = load_publish_plugin_module()


class PluginPublisherTests(unittest.TestCase):
    def test_publish_moves_complete_generation_then_updates_pointer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "plugin"
            incoming = root / ".incoming"
            incoming.mkdir(parents=True)
            (incoming / "plugin.toml").write_text("schema_version = 1\n")
            (incoming / "plugin.dll").write_bytes(b"first")

            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "publish_plugin.py"),
                    "--root",
                    str(root),
                    "--incoming",
                    str(incoming),
                ],
                check=True,
            )
            pointer = json.loads((root / "current.json").read_text())
            generation = root / "generations" / pointer["generation"]
            self.assertTrue((generation / "plugin.toml").is_file())
            self.assertEqual(b"first", (generation / "plugin.dll").read_bytes())
            package = json.loads((generation / "package.json").read_text())
            self.assertEqual(pointer["generation"], package["build_id"])
            self.assertIn("plugin.dll", package["files"])

    def test_atomic_replace_retries_transient_permission_error(self) -> None:
        denied = PermissionError(13, "temporarily locked")
        with (
            mock.patch.object(
                publish_plugin.os, "replace", side_effect=[denied, denied, None]
            ) as replace,
            mock.patch.object(publish_plugin.time, "sleep") as sleep,
        ):
            publish_plugin.replace_with_retry(
                pathlib.Path("incoming"), pathlib.Path("published")
            )

        self.assertEqual(3, replace.call_count)
        self.assertEqual(2, sleep.call_count)


def load_do_module():
    spec = importlib.util.spec_from_file_location("draxul_do", ROOT / "do.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load do.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_sdk_smoke_module():
    spec = importlib.util.spec_from_file_location(
        "draxul_sdk_smoke", ROOT / "tests" / "support" / "sdk_smoke.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load tests/support/sdk_smoke.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


draxul_do = load_do_module()
sdk_smoke = load_sdk_smoke_module()


class ExternalSdkSmokeCommandTests(unittest.TestCase):
    def make_args(self, generator: str, *, platform: str = "", toolset: str = ""):
        return sdk_smoke.make_argument_parser().parse_args(
            [
                "--cmake", "cmake",
                "--source-root", "source-root",
                "--build-root", "build-root",
                "--draxul", "draxul",
                "--config", "Debug",
                "--generator", generator,
                f"--platform={platform}",
                f"--toolset={toolset}",
                "--c-compiler=C:/toolchain/cl.exe",
                "--cxx-compiler=C:/toolchain/cl.exe",
                "--make-program=C:/tools/ninja.exe",
                "--toolchain-file=C:/toolchain/parent.cmake",
            ]
        )

    def test_single_config_external_build_inherits_parent_toolchain(self) -> None:
        args = self.make_args("Ninja")

        with mock.patch.object(sdk_smoke, "run") as run:
            sdk_smoke.configure_external(
                args, pathlib.Path("fixture"), pathlib.Path("out"), pathlib.Path("sdk")
            )

        command = run.call_args.args[0]
        self.assertIn("-DCMAKE_BUILD_TYPE=Debug", command)
        self.assertIn("-DCMAKE_C_COMPILER=C:/toolchain/cl.exe", command)
        self.assertIn("-DCMAKE_CXX_COMPILER=C:/toolchain/cl.exe", command)
        self.assertIn("-DCMAKE_MAKE_PROGRAM=C:/tools/ninja.exe", command)
        self.assertIn("-DCMAKE_TOOLCHAIN_FILE=C:/toolchain/parent.cmake", command)

    def test_ide_external_build_uses_generator_platform_and_toolset(self) -> None:
        args = self.make_args(
            "Visual Studio 17 2022", platform="x64", toolset="v143"
        )

        with mock.patch.object(sdk_smoke, "run") as run:
            sdk_smoke.configure_external(
                args, pathlib.Path("fixture"), pathlib.Path("out"), pathlib.Path("sdk")
            )

        command = run.call_args.args[0]
        self.assertIn("-A", command)
        self.assertIn("x64", command)
        self.assertIn("-T", command)
        self.assertIn("v143", command)
        self.assertFalse(any(argument.startswith("-DCMAKE_C_") for argument in command))
        self.assertFalse(any(argument.startswith("-DCMAKE_CXX_") for argument in command))
        self.assertNotIn("-DCMAKE_MAKE_PROGRAM=C:/tools/ninja.exe", command)
        self.assertIn("-DCMAKE_TOOLCHAIN_FILE=C:/toolchain/parent.cmake", command)


class RenderManifestTests(unittest.TestCase):
    def make_manifest_root(self, document: dict) -> tempfile.TemporaryDirectory:
        tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(tmp.name)
        render_dir = root / "tests" / "render"
        reference_dir = render_dir / "reference"
        reference_dir.mkdir(parents=True)
        (render_dir / "manifest.json").write_text(json.dumps(document), encoding="utf-8")
        for scenario in document["scenarios"]:
            (render_dir / f"{scenario['name']}.toml").write_text("[window]\n", encoding="utf-8")
            if scenario.get("reference_required"):
                for platform in scenario["platforms"]:
                    (reference_dir / f"{scenario['name']}.{platform}.bmp").write_bytes(b"BM")
        return tmp

    def test_repository_manifest_is_complete_and_drives_matching_ctest_renderall_inventory(self) -> None:
        scenarios = draxul_do.load_render_manifest(ROOT)
        ctest = [scenario["name"] for scenario in scenarios if scenario["ctest"]]
        renderall = draxul_do.render_scenario_names(ROOT, "renderall")

        self.assertEqual(ctest, renderall)
        self.assertIn("nanovg-demo", ctest)
        self.assertNotIn("wide-char-scroll", ctest)
        self.assertNotIn("ligatures-view", [scenario["name"] for scenario in scenarios])

    def test_missing_toml_is_rejected(self) -> None:
        document = json.loads((ROOT / "tests" / "render" / "manifest.json").read_text())
        with self.make_manifest_root(document) as tmp:
            root = pathlib.Path(tmp)
            (root / "tests" / "render" / "basic-view.toml").unlink()
            with self.assertRaisesRegex(ValueError, "missing=.*basic-view"):
                draxul_do.load_render_manifest(root)

    def test_missing_platform_reference_is_rejected(self) -> None:
        document = json.loads((ROOT / "tests" / "render" / "manifest.json").read_text())
        with self.make_manifest_root(document) as tmp:
            root = pathlib.Path(tmp)
            (root / "tests" / "render" / "reference" / "basic-view.macos.bmp").unlink()
            with self.assertRaisesRegex(ValueError, "basic-view.macos.bmp"):
                draxul_do.load_render_manifest(root)

    def test_duplicate_name_and_unknown_field_are_rejected(self) -> None:
        document = json.loads((ROOT / "tests" / "render" / "manifest.json").read_text())
        duplicate = dict(document["scenarios"][0])
        document["scenarios"].append(duplicate)
        with self.make_manifest_root(document) as tmp:
            with self.assertRaisesRegex(ValueError, "duplicate render scenario"):
                draxul_do.load_render_manifest(pathlib.Path(tmp))

        document = json.loads((ROOT / "tests" / "render" / "manifest.json").read_text())
        document["scenarios"][0]["mystery"] = True
        with self.make_manifest_root(document) as tmp:
            with self.assertRaisesRegex(ValueError, "unknown=.*mystery"):
                draxul_do.load_render_manifest(pathlib.Path(tmp))

    def test_orphaned_toml_and_reference_are_rejected(self) -> None:
        document = json.loads((ROOT / "tests" / "render" / "manifest.json").read_text())
        with self.make_manifest_root(document) as tmp:
            root = pathlib.Path(tmp)
            (root / "tests" / "render" / "orphan.toml").write_text("[window]\n")
            with self.assertRaisesRegex(ValueError, "orphaned=.*orphan"):
                draxul_do.load_render_manifest(root)

        with self.make_manifest_root(document) as tmp:
            root = pathlib.Path(tmp)
            (root / "tests" / "render" / "reference" / "orphan.windows.bmp").write_bytes(b"BM")
            with self.assertRaisesRegex(ValueError, "orphaned=.*orphan.windows.bmp"):
                draxul_do.load_render_manifest(root)


class MegacityParserArgumentTests(unittest.TestCase):
    def test_graphify_parser_is_rejected_for_megacity(self) -> None:
        with self.assertRaisesRegex(ValueError, "--parser must be one of: treesitter, treesitter_db"):
            draxul_do._consume_megacity_parser_args(
                ["--host", "megacity", "--parser", "graphify", "--console"]
            )

    def test_treesitter_parser_accepts_alias_and_normalizes(self) -> None:
        app_args, parser = draxul_do._consume_megacity_parser_args(
            ["--host", "megacity", "--parser", "treesitter"]
        )

        self.assertEqual(["--host", "megacity"], app_args)
        self.assertEqual("treesitter_db", parser)

    def test_parser_requires_megacity_host(self) -> None:
        with self.assertRaisesRegex(ValueError, "--parser is only supported with --host megacity"):
            draxul_do._consume_megacity_parser_args(["--host", "nvim", "--parser", "treesitter"])

    def test_parser_requires_value(self) -> None:
        with self.assertRaisesRegex(ValueError, "--parser requires a value"):
            draxul_do._consume_megacity_parser_args(["--host", "megacity", "--parser"])


class MegacityConfigMergeTests(unittest.TestCase):
    def test_treesitter_parser_appends_missing_section(self) -> None:
        merged = draxul_do._merge_megacity_parser_config("font_size = 14\n", "treesitter")

        self.assertEqual(
            'font_size = 14\n\n[mega_city_code]\ncode_source = "treesitter_db"\n',
            merged,
        )

    def test_treesitter_parser_updates_existing_section_and_removes_graphify_path(self) -> None:
        merged = draxul_do._merge_megacity_parser_config(
            'font_size = 14\n\n[mega_city_code]\nshow_ui_panels = true\ncode_source = "graphify"\ngraphify_graph_path = "graphify-out/graph.json"\n\n[terminal]\nfg = "#fff"\n',
            "treesitter",
        )

        self.assertIn('[mega_city_code]\nshow_ui_panels = true\ncode_source = "treesitter_db"\n\n[terminal]', merged)
        self.assertNotIn("graphify_graph_path", merged)
        self.assertIn('fg = "#fff"', merged)

    def test_treesitter_parser_updates_code_source_and_removes_graphify_path(self) -> None:
        merged = draxul_do._merge_megacity_parser_config(
            '[mega_city_code]\ncode_source = "graphify"\ngraphify_graph_path = "graphify-out/graph.json"\n',
            "treesitter_db",
        )

        self.assertEqual(
            '[mega_city_code]\ncode_source = "treesitter_db"\n',
            merged,
        )


class BuildCacheTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform.startswith("win"), "Windows executable layout")
    def test_draxul_exe_accepts_single_config_ninja_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bd = pathlib.Path(tmp)
            exe = bd / "draxul.exe"
            exe.write_text("")

            self.assertEqual(exe, draxul_do.draxul_exe(bd, "Release"))

    def test_incomplete_ninja_multi_config_cache_requires_configure(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bd = pathlib.Path(tmp)
            cache_file = bd / "CMakeCache.txt"
            cache_file.write_text("CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n")

            self.assertEqual(
                bd / "build.ninja",
                draxul_do._missing_generated_build_file(cache_file, bd, "Release"),
            )

            (bd / "build.ninja").write_text("")

            self.assertEqual(
                bd / "build-Release.ninja",
                draxul_do._missing_generated_build_file(cache_file, bd, "Release"),
            )

            (bd / "build-Release.ninja").write_text("")

            self.assertIsNone(draxul_do._missing_generated_build_file(cache_file, bd, "Release"))


class CleanCommandTests(unittest.TestCase):
    def test_help_lists_clean_command(self) -> None:
        self.assertIn("clean        Remove repository build directories", draxul_do.help_text())

    def test_clean_removes_all_build_trees_and_preserves_neighboring_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            build_names = (
                "build",
                "build-ninja",
                "build-ninja-debug",
                "build-ninja-release",
                "build-ninja-relwithdebinfo",
                "build-ninja-satview-off",
                "build-tools",
            )
            for build_name in build_names:
                build_file = root / build_name / "CMakeFiles" / "cache.txt"
                build_file.parent.mkdir(parents=True)
                build_file.write_text("generated")
            deploy_file = root / "deploy" / "package.zip"
            deploy_file.parent.mkdir()
            deploy_file.write_text("keep")
            source_file = root / "builder" / "README.md"
            source_file.parent.mkdir()
            source_file.write_text("keep")
            similarly_named_file = root / "build-not-a-directory"
            similarly_named_file.write_text("keep")
            output = io.StringIO()

            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "clean"]),
            ):
                self.assertEqual(0, draxul_do.main())

            for build_name in build_names:
                self.assertFalse((root / build_name).exists())
            self.assertEqual("keep", deploy_file.read_text())
            self.assertEqual("keep", source_file.read_text())
            self.assertEqual("keep", similarly_named_file.read_text())
            self.assertIn("Removing build directory", output.getvalue())

    def test_clean_succeeds_when_build_is_already_absent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            output = io.StringIO()

            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "clean"]),
            ):
                self.assertEqual(0, draxul_do.main())

            self.assertIn("Build directories already absent", output.getvalue())

    def test_clean_rejects_arguments_without_removing_build(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            build_file = root / "build" / "CMakeFiles" / "cache.txt"
            build_file.parent.mkdir(parents=True)
            build_file.write_text("generated")
            error = io.StringIO()

            with (
                contextlib.redirect_stderr(error),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "clean", "--help"]),
            ):
                self.assertEqual(2, draxul_do.main())

            self.assertEqual("generated", build_file.read_text())
            self.assertEqual(
                "ERROR: clean does not accept arguments: --help\n",
                error.getvalue(),
            )


class TestCommandTests(unittest.TestCase):
    def test_help_describes_fast_unit_scope(self) -> None:
        help_output = draxul_do.help_text()

        self.assertIn("run core unit tests in parallel", help_output.lower())
        self.assertIn("--satview", help_output)
        self.assertIn("--scoreview", help_output)
        self.assertIn("--rezonality", help_output)
        self.assertIn("--products", help_output)
        self.assertIn("--all", help_output)
        self.assertNotIn("test         Run the full local test suite", help_output)

    def test_test_command_uses_default_debug_ninja_cache_and_parallel_ctest(self) -> None:
        build_dir = ROOT / "build-ninja-debug"
        build_env = {"DRAXUL_TEST_ENV": "1"}
        with (
            mock.patch.object(draxul_do.sys, "argv", ["do.py", "test"]),
            mock.patch.object(
                draxul_do,
                "_configure_and_build",
                return_value=(0, build_dir, "Debug", build_env),
            ) as build_mock,
            mock.patch.object(draxul_do, "run", return_value=0) as run_mock,
        ):
            self.assertEqual(0, draxul_do.main())

        build_mock.assert_called_once_with(
            ROOT,
            "debug",
            False,
            "ninja",
            targets=("draxul-tests-core",),
        )
        _, ctest_filter, _ = draxul_do._test_scope_selection(set(), False)
        run_mock.assert_called_once_with(
            [
                "ctest",
                "--test-dir", str(build_dir),
                "--build-config", "Debug",
                "--parallel", draxul_do._test_parallel_jobs(),
                "--timeout", "120",
                *ctest_filter,
                "--output-on-failure",
            ],
            ROOT,
            env=build_env,
        )

    def test_test_command_passes_release_vs_and_verbose_selection(self) -> None:
        build_dir = ROOT / "build"
        with (
            mock.patch.object(
                draxul_do.sys,
                "argv",
                ["do.py", "test", "release", "--vs", "--verbose"],
            ),
            mock.patch.object(
                draxul_do,
                "_configure_and_build",
                return_value=(0, build_dir, "Release", None),
            ) as build_mock,
            mock.patch.object(draxul_do, "run", return_value=0) as run_mock,
        ):
            self.assertEqual(0, draxul_do.main())

        build_mock.assert_called_once_with(
            ROOT,
            "release",
            False,
            "vs",
            targets=("draxul-tests-core",),
        )
        self.assertEqual("--verbose", run_mock.call_args.args[0][-1])

    def test_individual_product_scopes_add_targets_and_ctest_names(self) -> None:
        targets, ctest_filter, label = draxul_do._test_scope_selection(
            {"satview", "scoreview"}, False
        )

        self.assertEqual(
            (
                "draxul-tests-core",
                "draxul-tests-satview",
                "draxul-tests-scoreview",
            ),
            targets,
        )
        self.assertEqual("--tests-regex", ctest_filter[0])
        self.assertIn("draxul-test-satview-shard", ctest_filter[1])
        self.assertIn("draxul-satview-catalog-py-tests", ctest_filter[1])
        self.assertIn("draxul-test-scoreview-shard", ctest_filter[1])
        self.assertIn("draxul-test-scoreview-runtime-shard", ctest_filter[1])
        self.assertNotIn("draxul-test-megacity-shard", ctest_filter[1])
        self.assertEqual("core + satview, scoreview", label)

    def test_products_scope_selects_every_product(self) -> None:
        parsed = draxul_do._parse_test_args(["--products"])
        targets, ctest_filter, label = draxul_do._test_scope_selection(
            parsed[4], parsed[5]
        )

        self.assertEqual(
            (
                "draxul-tests-core",
                "draxul-tests-megacity",
                "draxul-tests-satview",
                "draxul-tests-scoreview",
                "draxul-tests-rezonality",
            ),
            targets,
        )
        for product in ("megacity", "satview", "scoreview", "rezonality"):
            self.assertIn(f"draxul-test-{product}-shard", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-pbr-robot", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-ray-tracer", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-audio-spectrum", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-plugin", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-blend-waves", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-deferred-shading", ctest_filter[1])
        self.assertIn(
            "draxul-render-rezonality-protoplanetary-disc", ctest_filter[1]
        )
        self.assertIn("draxul-rezonality-agent-layout", ctest_filter[1])
        self.assertEqual(
            "core + megacity, satview, scoreview, rezonality", label
        )

    def test_all_scope_uses_complete_unit_aggregate(self) -> None:
        parsed = draxul_do._parse_test_args(["--all"])

        self.assertEqual(
            (("draxul-tests",), ["--label-regex", "unit"], "all unit tests"),
            draxul_do._test_scope_selection(parsed[4], parsed[5]),
        )

    def test_test_command_rejects_app_arguments(self) -> None:
        error = io.StringIO()
        with (
            contextlib.redirect_stderr(error),
            mock.patch.object(draxul_do.sys, "argv", ["do.py", "test", "--console"]),
            mock.patch.object(draxul_do, "_configure_and_build") as build_mock,
        ):
            self.assertEqual(2, draxul_do.main())

        build_mock.assert_not_called()
        self.assertIn("test accepts", error.getvalue())


class SmokeCommandTests(unittest.TestCase):
    def test_smoke_can_reuse_selected_debug_build_without_rebuilding(self) -> None:
        executable = ROOT / "build-ninja-debug" / "draxul.exe"
        build_env = {"DRAXUL_TEST_ENV": "1"}
        with (
            mock.patch.object(
                draxul_do.sys,
                "argv",
                ["do.py", "smoke", "--skip-build"],
            ),
            mock.patch.object(
                draxul_do,
                "build_shortcut_exe",
                return_value=(0, executable, build_env),
            ) as build_mock,
            mock.patch.object(draxul_do, "run", return_value=0) as run_mock,
        ):
            self.assertEqual(0, draxul_do.main())

        build_mock.assert_called_once_with(
            ROOT,
            mode="debug",
            force_reconfigure=False,
            build_system="ninja",
            skip_build=True,
        )
        run_mock.assert_called_once_with(
            [str(executable), "--console", "--smoke-test"],
            ROOT,
            env=build_env,
        )

    def test_smoke_rejects_duplicate_skip_build(self) -> None:
        error = io.StringIO()
        with (
            contextlib.redirect_stderr(error),
            mock.patch.object(
                draxul_do.sys,
                "argv",
                ["do.py", "smoke", "--skip-build", "--skip-build"],
            ),
        ):
            self.assertEqual(2, draxul_do.main())

        self.assertIn("only once", error.getvalue())


class DeployPackagingTests(unittest.TestCase):
    def test_deploy_args_default_to_release_build_flags(self) -> None:
        force_reconfigure, build_system = draxul_do._parse_deploy_args([])

        self.assertFalse(force_reconfigure)
        self.assertEqual("ninja", build_system)

    def test_deploy_args_reject_debug_mode(self) -> None:
        with self.assertRaisesRegex(ValueError, "deploy always creates a release build"):
            draxul_do._parse_deploy_args(["debug"])

    def test_deploy_output_paths_use_date_and_platform_folder(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)

            platform_dir, archive_path = draxul_do._deploy_output_paths(root, "2026_07_03", "darwin")

            self.assertEqual(root / "deploy" / "2026_07_03" / "mac", platform_dir)
            self.assertEqual(root / "deploy" / "2026_07_03" / "draxul-2026_07_03-mac.zip", archive_path)

    def test_windows_deploy_payload_source_is_executable_not_build_directory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = pathlib.Path(tmp)
            executable = build_dir / "draxul.exe"
            executable.write_text("exe")

            self.assertEqual(
                executable,
                draxul_do._deploy_payload_source(build_dir, "Release", "win32"),
            )

            visual_studio_executable = build_dir / "Release" / "draxul.exe"
            visual_studio_executable.parent.mkdir()
            visual_studio_executable.write_text("vs exe")
            self.assertEqual(
                visual_studio_executable,
                draxul_do._deploy_payload_source(build_dir, "Release", "win32"),
            )

    def test_stage_deploy_payload_replaces_existing_folder_and_zips_payload(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source = root / "build" / "draxul.app"
            (source / "Contents" / "MacOS").mkdir(parents=True)
            (source / "Contents" / "MacOS" / "draxul").write_text("exe")
            platform_dir = root / "deploy" / "2026_07_03" / "mac"
            archive_path = root / "deploy" / "2026_07_03" / "draxul-2026_07_03-mac.zip"
            stale_file = platform_dir / "old.txt"
            stale_file.parent.mkdir(parents=True)
            stale_file.write_text("stale")

            draxul_do._stage_deploy_payload(source, platform_dir, archive_path)

            self.assertFalse(stale_file.exists())
            self.assertTrue((platform_dir / "draxul.app" / "Contents" / "MacOS" / "draxul").exists())
            self.assertTrue(archive_path.exists())
            with zipfile.ZipFile(archive_path) as archive:
                self.assertIn("mac/draxul.app/Contents/MacOS/draxul", archive.namelist())

    def test_stage_windows_deploy_payload_excludes_build_outputs_and_sources(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            build_dir = root / "build"
            executable = build_dir / "draxul.exe"
            executable.parent.mkdir(parents=True)
            executable.write_text("exe")
            (build_dir / "runtime.dll").write_text("dll")
            (build_dir / "assets" / "satview").mkdir(parents=True)
            (build_dir / "assets" / "satview" / "earth.jpg").write_text("asset")
            (build_dir / "fonts").mkdir()
            (build_dir / "fonts" / "font.ttf").write_text("font")
            (build_dir / "shaders").mkdir()
            (build_dir / "shaders" / "grid.vert.spv").write_text("shader")
            runtime_dir = root / "windows-runtime"
            runtime_dir.mkdir()
            for library_name in draxul_do.WINDOWS_CRT_RUNTIME_LIBRARIES:
                (runtime_dir / library_name).write_text("runtime")

            (build_dir / "CMakeFiles" / "draxul.dir" / "app").mkdir(parents=True)
            (build_dir / "CMakeFiles" / "draxul.dir" / "app" / "main.cpp.obj").write_text("object")
            (build_dir / "modules" / "satview").mkdir(parents=True)
            (build_dir / "modules" / "satview" / "satview.lib").write_text("library")
            (build_dir / "CMakeCache.txt").write_text("cache")
            (build_dir / "draxul.lib").write_text("library")
            (build_dir / "draxul.pdb").write_text("symbols")

            platform_dir = root / "deploy" / "2026_07_03" / "win"
            archive_path = root / "deploy" / "2026_07_03" / "draxul-2026_07_03-win.zip"
            stale_file = platform_dir / "_deps" / "dependency-src" / ".git" / "objects" / "stale.idx"
            stale_file.parent.mkdir(parents=True)
            stale_file.write_text("stale")
            stale_file.chmod(stat.S_IREAD)
            draxul_do._stage_deploy_payload(
                executable,
                platform_dir,
                archive_path,
                windows_runtime_directory=runtime_dir,
            )

            self.assertEqual(
                {
                    "assets/satview/earth.jpg",
                    "draxul.exe",
                    "fonts/font.ttf",
                    "msvcp140.dll",
                    "msvcp140_atomic_wait.dll",
                    "runtime.dll",
                    "shaders/grid.vert.spv",
                    "vcruntime140.dll",
                    "vcruntime140_1.dll",
                },
                {
                    path.relative_to(platform_dir).as_posix()
                    for path in platform_dir.rglob("*")
                    if path.is_file()
                },
            )
            with zipfile.ZipFile(archive_path) as archive:
                archived_files = {
                    name
                    for name in archive.namelist()
                    if not name.endswith("/")
                }
                self.assertEqual(
                    {
                        "win/assets/satview/earth.jpg",
                        "win/draxul.exe",
                        "win/fonts/font.ttf",
                        "win/msvcp140.dll",
                        "win/msvcp140_atomic_wait.dll",
                        "win/runtime.dll",
                        "win/shaders/grid.vert.spv",
                        "win/vcruntime140.dll",
                        "win/vcruntime140_1.dll",
                    },
                    archived_files,
                )


class HygieneCommandTests(unittest.TestCase):
    def test_help_lists_hygiene_command(self) -> None:
        self.assertIn("hygiene", draxul_do.help_text())

    def test_forbidden_artifacts_flags_root_and_anywhere_offenders(self) -> None:
        offenders = draxul_do.forbidden_artifacts(
            [
                "key.txt",
                "megacity-linux-drivers-mesh.bmp",
                "NUL.obj",
                "debug.log",
                "default.profraw",
                ".DS_Store",
                ".!75583!.DS_Store",
                "sub/dir/.DS_Store",
                "coverage/report.profdata",
            ]
        )
        self.assertEqual(
            [
                ".!75583!.DS_Store",
                ".DS_Store",
                "NUL.obj",
                "coverage/report.profdata",
                "debug.log",
                "default.profraw",
                "key.txt",
                "megacity-linux-drivers-mesh.bmp",
                "sub/dir/.DS_Store",
            ],
            offenders,
        )

    def test_forbidden_artifacts_allows_legitimate_source_and_assets(self) -> None:
        offenders = draxul_do.forbidden_artifacts(
            [
                "app/app.cpp",
                "docs/features.md",
                "modules/megacity/assets/tree.obj",  # nested mesh asset
                "tests/render/reference/basic-view.macos.bmp",  # nested render reference
                "kanban/ice-box/22 inputdispatcher-null-deps -test.md",  # 'nul' substring only
                "CMakeLists.txt",
            ]
        )
        self.assertEqual([], offenders)

    def test_feature_doc_problems_accepts_short_pointer(self) -> None:
        self.assertEqual(
            [],
            draxul_do.feature_doc_problems("See docs/features.md for the inventory.\n", True),
        )

    def test_feature_doc_problems_flags_missing_canonical_inventory(self) -> None:
        self.assertIn(
            "docs/features.md (the canonical feature inventory) is missing",
            draxul_do.feature_doc_problems("See docs/features.md\n", False),
        )

    def test_feature_doc_problems_flags_duplicate_inventory_and_missing_pointer(self) -> None:
        duplicate_inventory = "# Features\n" + "\n".join(f"- feature {n}" for n in range(60))
        problems = draxul_do.feature_doc_problems(duplicate_inventory, True)
        self.assertTrue(any("short pointer to docs/features.md" in problem for problem in problems))
        self.assertTrue(any("must point to docs/features.md" in problem for problem in problems))

    def _clean_hygiene_root(self, root: pathlib.Path) -> None:
        (root / "FEATURES.md").write_text("See docs/features.md for features.\n", encoding="utf-8")
        (root / "docs").mkdir()
        (root / "docs" / "features.md").write_text("# Inventory\n", encoding="utf-8")

    def test_hygiene_command_passes_on_clean_tree(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self._clean_hygiene_root(root)
            output = io.StringIO()
            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do, "tracked_files", return_value=["app/app.cpp", "FEATURES.md"]),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "hygiene"]),
            ):
                self.assertEqual(0, draxul_do.main())
            self.assertIn("Hygiene check passed", output.getvalue())

    def test_hygiene_command_fails_on_forbidden_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self._clean_hygiene_root(root)
            output = io.StringIO()
            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do, "tracked_files", return_value=["key.txt", "app/app.cpp"]),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "hygiene"]),
            ):
                self.assertEqual(1, draxul_do.main())
            self.assertIn("forbidden tracked artifact: key.txt", output.getvalue())

    def test_hygiene_rejects_arguments(self) -> None:
        error = io.StringIO()
        with (
            contextlib.redirect_stderr(error),
            mock.patch.object(draxul_do.sys, "argv", ["do.py", "hygiene", "--oops"]),
        ):
            self.assertEqual(2, draxul_do.main())
        self.assertIn("hygiene does not accept arguments: --oops", error.getvalue())

    def test_repository_passes_hygiene(self) -> None:
        # Integration guard: the real tree must stay free of forbidden artifacts
        # and keep a single feature-doc source of truth.
        self.assertEqual(0, draxul_do.cmd_hygiene(ROOT))


class KanbanReportTests(unittest.TestCase):
    def test_help_lists_kanban_report_command(self) -> None:
        self.assertIn("kanban-report", draxul_do.help_text())

    def test_count_task_boxes_counts_checked_and_unchecked(self) -> None:
        text = "# Card\n- [x] done one\n- [X] done two\n- [ ] open one\n  - [ ] nested open\nplain - [ ] not a box\n"
        self.assertEqual((2, 2), draxul_do.count_task_boxes(text))

    def test_lane_cards_sorts_and_excludes_readme(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            lane = pathlib.Path(tmp)
            (lane / "02 b -bug.md").write_text("b")
            (lane / "01 a -feature.md").write_text("a")
            (lane / "README.md").write_text("readme")
            (lane / "notes.txt").write_text("ignored")
            names = [card.name for card in draxul_do.lane_cards(lane)]
            self.assertEqual(["01 a -feature.md", "02 b -bug.md"], names)

    def test_kanban_report_flags_ambiguous_done_and_ready_pending_without_editing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            board = root / "kanban"
            (board / "pending").mkdir(parents=True)
            (board / "done").mkdir(parents=True)
            done_card = board / "done" / "01 shipped -feature.md"
            done_card.write_text("# Shipped\n- [x] built\n- [ ] follow-up left open\n", encoding="utf-8")
            ready_card = board / "pending" / "02 ready -refactor.md"
            ready_card.write_text("# Ready\n- [x] one\n- [x] two\n", encoding="utf-8")
            done_before = done_card.read_text(encoding="utf-8")
            output = io.StringIO()

            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "kanban-report"]),
            ):
                self.assertEqual(0, draxul_do.main())

            report = output.getvalue()
            self.assertIn("Ambiguous done cards (1)", report)
            self.assertIn("01 shipped -feature.md", report)
            self.assertIn("Fully-ticked pending cards (1)", report)
            self.assertIn("02 ready -refactor.md", report)
            # Read-only: the report must never rewrite a card.
            self.assertEqual(done_before, done_card.read_text(encoding="utf-8"))

    def test_kanban_report_reports_clean_done_lane(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            done = root / "kanban" / "done"
            done.mkdir(parents=True)
            (done / "01 clean -bug.md").write_text("# Clean\n- [x] all ticked\n", encoding="utf-8")
            output = io.StringIO()
            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "kanban-report"]),
            ):
                self.assertEqual(0, draxul_do.main())
            self.assertIn("No ambiguous done cards", output.getvalue())

    def test_kanban_report_rejects_arguments(self) -> None:
        error = io.StringIO()
        with (
            contextlib.redirect_stderr(error),
            mock.patch.object(draxul_do.sys, "argv", ["do.py", "kanban-report", "extra"]),
        ):
            self.assertEqual(2, draxul_do.main())
        self.assertIn("kanban-report does not accept arguments: extra", error.getvalue())

    def test_repository_kanban_report_runs(self) -> None:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(0, draxul_do.cmd_kanban_report(ROOT))
        self.assertIn("Kanban report", output.getvalue())


if __name__ == "__main__":
    unittest.main()
