from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import signal
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


def load_script_module(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load scripts/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


draxul_do = load_do_module()
sdk_smoke = load_sdk_smoke_module()
draxul_paths = load_script_module("draxul_paths")


class DeveloperHelperPathTests(unittest.TestCase):
    def test_platform_executable_paths_cover_bundle_windows_and_unix(self) -> None:
        root = pathlib.Path("workspace")
        self.assertEqual(
            root / "build" / "draxul.app" / "Contents" / "MacOS" / "draxul",
            draxul_paths.executable_path(root, platform="darwin"),
        )
        self.assertEqual(
            root / "build" / "draxul",
            draxul_paths.executable_path(root, platform="linux"),
        )
        self.assertEqual(
            root / "build" / "draxul.exe",
            draxul_paths.executable_path(root, platform="win32"),
        )

    @unittest.skipUnless(sys.platform == "darwin", "macOS shell helper")
    def test_store_logs_resolves_the_standard_bundle_executable(self) -> None:
        completed = subprocess.run(
            ["bash", str(ROOT / "scripts" / "store_logs.sh"), "--print-executable"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(
            ROOT / "build" / "draxul.app" / "Contents" / "MacOS" / "draxul",
            pathlib.Path(completed.stdout.strip()).resolve(),
        )


class AgentGuidanceReferenceTests(unittest.TestCase):
    def test_current_guidance_avoids_retired_product_paths_and_interfaces(self) -> None:
        guidance_paths = [
            ROOT / "AGENTS.md",
            ROOT / "CLAUDE.md",
            ROOT / "README.md",
            ROOT / "docs" / "module-map.md",
            ROOT / "plugins" / "megacity" / "product" / "AGENTS.md",
        ]
        current_guidance = "\n".join(
            path.read_text(encoding="utf-8") for path in guidance_paths
        )
        generated_guidance = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (
                ROOT / "docs" / "uml" / "draxul_classes.puml",
                ROOT / "docs" / "uml" / "draxul_classes.svg",
            )
        )
        for retired in (
            "modules/megacity/",
            "modules/satview/",
            "modules/score/",
            "I3DRenderer",
        ):
            self.assertNotIn(retired, current_guidance)
            self.assertNotIn(retired, generated_guidance)

        megacity_guidance = guidance_paths[-1].read_text(encoding="utf-8")
        self.assertNotIn("build-ninja-release", megacity_guidance)
        self.assertIn("do.py test debug --megacity", megacity_guidance)

    def test_named_aggregate_targets_exist_in_authoritative_cmake(self) -> None:
        tests_cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
        for target in (
            "draxul-tests-core",
            "draxul-tests-megacity",
            "draxul-tests-satview",
            "draxul-tests-scoreview",
            "draxul-tests-pcbview",
            "draxul-tests-rezonality",
        ):
            self.assertIn(target, tests_cmake)


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
            self.assertIsNone(
                draxul_do._missing_generated_build_file(cache_file, bd, "Release")
            )


class BuildTreeLockTests(unittest.TestCase):
    def test_live_owner_blocks_second_build_and_records_inspectable_result(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_tree = pathlib.Path(tmp) / "build"
            first = draxul_do.BuildTreeLock(build_tree, ["do.py", "test"])
            second = draxul_do.BuildTreeLock(build_tree, ["do.py", "build"])

            first.acquire()
            try:
                with self.assertRaisesRegex(
                    draxul_do.BuildTreeBusyError, rf"PID {first._owner()['pid']}"
                ):
                    second.acquire()
                first.finish(0)
            finally:
                first.release()

            self.assertFalse(first.path.exists())
            result = json.loads(first.result_path.read_text(encoding="utf-8"))
            self.assertEqual("completed", result["status"])
            self.assertEqual(0, result["return_code"])
            self.assertEqual(["do.py", "test"], result["command"])

    def test_stale_owner_is_recovered_without_touching_a_live_process(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build_tree = pathlib.Path(tmp) / "build"
            build_tree.mkdir()
            lock_path = build_tree / ".draxul-build.lock"
            lock_path.write_text(
                json.dumps({"pid": 424242, "token": "stale"}), encoding="utf-8"
            )
            lock = draxul_do.BuildTreeLock(build_tree, ["do.py", "build"])

            with mock.patch.object(
                draxul_do, "_process_is_running", return_value=False
            ) as process_is_running:
                lock.acquire()
            try:
                self.assertEqual(lock.token, lock._owner()["token"])
                process_is_running.assert_called_once_with(424242)
            finally:
                lock.release()

    def test_serialized_build_exits_cleanly_when_tree_is_owned(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            build_tree = draxul_do._selected_build_dir(root, "debug", "ninja")
            owner = draxul_do.BuildTreeLock(build_tree, ["do.py", "test"])
            owner.acquire()

            @draxul_do.serialized_build
            def should_not_run(*_args, **_kwargs):
                self.fail("contending build ran")

            errors = io.StringIO()
            try:
                with contextlib.redirect_stderr(errors):
                    result = should_not_run(root, "debug", False, "ninja")
            finally:
                owner.release()

            self.assertEqual(3, result[0])
            self.assertIn("build tree is owned by PID", errors.getvalue())

    def test_interrupted_build_records_result_and_releases_lock(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)

            @draxul_do.serialized_build
            def interrupted(*_args, **_kwargs):
                raise KeyboardInterrupt

            with self.assertRaises(KeyboardInterrupt):
                interrupted(root, "debug", False, "ninja")

            build_tree = draxul_do._selected_build_dir(root, "debug", "ninja")
            self.assertFalse((build_tree / ".draxul-build.lock").exists())
            result = json.loads(
                (build_tree / ".draxul-build-result.json").read_text(encoding="utf-8")
            )
            self.assertEqual("interrupted", result["status"])
            self.assertEqual(130, result["return_code"])


class RunCommandTests(unittest.TestCase):
    def test_windows_gui_launch_returns_after_starting_app(self) -> None:
        executable = ROOT / "build-ninja-release" / "draxul.exe"
        build_env = {"DRAXUL_TEST_ENV": "1"}
        completed = subprocess.CompletedProcess([], 0)
        with (
            mock.patch.object(draxul_do.sys, "platform", "win32"),
            mock.patch.object(
                draxul_do,
                "_configure_and_build",
                return_value=(
                    0,
                    ROOT / "build-ninja-release",
                    "Release",
                    build_env,
                ),
            ),
            mock.patch.object(draxul_do, "draxul_exe", return_value=executable),
            mock.patch.object(pathlib.Path, "exists", return_value=True),
            mock.patch.object(
                draxul_do.subprocess, "run", return_value=completed
            ) as run_mock,
        ):
            self.assertEqual(0, draxul_do.cmd_run(ROOT, ["release"]))

        run_mock.assert_called_once_with(
            ["cmd", "/c", "start", "", str(executable)],
            cwd=ROOT,
            check=False,
            env=build_env,
        )

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

    def test_clean_refuses_to_remove_a_live_owned_build_tree(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            owner = draxul_do.BuildTreeLock(root / "build", ["do.py", "test"])
            owner.acquire()
            errors = io.StringIO()
            try:
                with contextlib.redirect_stderr(errors):
                    self.assertEqual(3, draxul_do.cmd_clean(root))
                self.assertTrue(owner.path.exists())
                self.assertIn("refusing to remove active build tree", errors.getvalue())
            finally:
                owner.release()

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
        self.assertIn("--pcbview", help_output)
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
            mock.patch.object(
                draxul_do, "_ctest_selection_count", return_value=(0, 14, "")
            ),
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
        self.assertIn("draxul-test-weather-shard", ctest_filter[1])
        self.assertIn("draxul-test-markdown-layout-shard", ctest_filter[1])
        self.assertIn("draxul-test-kanban-core-shard", ctest_filter[1])
        self.assertIn("draxul-test-kanban-host-shard", ctest_filter[1])
        self.assertIn("draxul-test-nanovg-paint-shard", ctest_filter[1])
        self.assertIn("draxul-test-plugin-nanovg-shard", ctest_filter[1])
        self.assertIn("draxul-test-render-contracts-shard", ctest_filter[1])
        run_mock.assert_called_once_with(
            [
                "ctest",
                "--test-dir", str(build_dir),
                "--build-config", "Debug",
                "--parallel", draxul_do._test_parallel_jobs(),
                "--timeout", "120",
                "--no-tests=error",
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
            mock.patch.object(
                draxul_do, "_ctest_selection_count", return_value=(0, 14, "")
            ),
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
        self.assertNotIn("draxul-test-megacity-parser-shard", ctest_filter[1])
        self.assertEqual("core + satview, scoreview", label)

    def test_label_filters_the_selected_scope_and_rejects_zero_matches(self) -> None:
        build_dir = ROOT / "build-ninja-debug"
        build_env = {"DRAXUL_TEST_ENV": "1"}
        with (
            mock.patch.object(
                draxul_do,
                "_configure_and_build",
                return_value=(0, build_dir, "Debug", build_env),
            ) as build_mock,
            mock.patch.object(draxul_do, "run", return_value=0) as run_mock,
            mock.patch.object(
                draxul_do, "_ctest_selection_count", return_value=(0, 2, "")
            ),
        ):
            self.assertEqual(
                0,
                draxul_do.cmd_test(ROOT, ["--satview", "--label", "kanban"]),
            )

        build_mock.assert_called_once_with(
            ROOT,
            "debug",
            False,
            "ninja",
            targets=("draxul-tests-core", "draxul-tests-satview"),
        )
        command = run_mock.call_args.args[0]
        self.assertIn("--tests-regex", command)
        self.assertIn("--label-regex", command)
        self.assertIn("^kanban$", command)
        self.assertIn("--no-tests=error", command)

    def test_label_requires_one_non_option_value(self) -> None:
        for args in (["--label"], ["--label", "--satview"]):
            with self.subTest(args=args):
                with self.assertRaisesRegex(ValueError, "--label requires a label"):
                    draxul_do._parse_test_args(args)

        with self.assertRaisesRegex(ValueError, "--label may be specified only once"):
            draxul_do._parse_test_args(["--label", "kanban", "--label", "unit"])

    def test_agent_integration_label_selects_the_core_focused_target(self) -> None:
        targets, ctest_filter, label = draxul_do._test_scope_selection(
            set(), False, "agent-integration"
        )

        self.assertEqual(("draxul-tests-core",), targets)
        self.assertEqual("--tests-regex", ctest_filter[0])
        self.assertIn("draxul-test-agent-integration-shard", ctest_filter[1])
        self.assertEqual(
            ["--label-regex", "^agent\\-integration$"], ctest_filter[2:]
        )
        self.assertEqual("core labeled agent-integration", label)
        tests_cmake = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            'draxul_add_test_target(draxul-test-agent-integration "agent-integration"',
            tests_cmake,
        )

    def test_unit_compatibility_flag_keeps_the_default_scope(self) -> None:
        parsed = draxul_do._parse_test_args(["--unit"])
        self.assertEqual(set(), parsed[4])
        self.assertFalse(parsed[5])
        self.assertIsNone(parsed[6])

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
                "draxul-tests-pcbview",
                "draxul-tests-rezonality",
            ),
            targets,
        )
        for product in ("megacity", "satview", "scoreview", "pcbview", "rezonality"):
            self.assertIn(f"draxul-test-{product}-shard", ctest_filter[1])
        self.assertIn("draxul-test-megacity-parser-shard", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-pbr-robot", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-ray-tracer", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-audio-spectrum", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-plugin", ctest_filter[1])
        self.assertIn("draxul-test-rezonality-project-shard", ctest_filter[1])
        self.assertIn("draxul-test-rezonality-runtime-shard", ctest_filter[1])
        self.assertIn("draxul-test-rezonality-audio-shard", ctest_filter[1])
        self.assertIn("draxul-render-pcbview-plugin", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-blend-waves", ctest_filter[1])
        self.assertIn("draxul-render-rezonality-deferred-shading", ctest_filter[1])
        self.assertIn(
            "draxul-render-rezonality-protoplanetary-disc", ctest_filter[1]
        )
        self.assertIn("draxul-rezonality-agent-layout", ctest_filter[1])
        self.assertEqual(
            "core + megacity, satview, scoreview, pcbview, rezonality", label
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

    def test_focused_target_preflights_filter_and_repeats_with_new_seeds(self) -> None:
        build_tree = ROOT / "build"
        executable = build_tree / "tests" / "draxul-test-core"
        with (
            mock.patch.object(
                draxul_do,
                "_configure_and_build",
                return_value=(0, build_tree, "Debug", None),
            ) as build_mock,
            mock.patch.object(
                draxul_do, "_focused_test_executable", return_value=executable
            ),
            mock.patch.object(pathlib.Path, "is_file", return_value=True),
            mock.patch.object(
                draxul_do,
                "_capture_owned_process",
                return_value=(0, "3 matching test cases\n"),
            ) as capture,
            mock.patch.object(draxul_do, "run", return_value=0) as run_mock,
        ):
            self.assertEqual(
                0,
                draxul_do.cmd_test(
                    ROOT,
                    ["--target", "draxul-test-core", "--catch", "[server]",
                     "--repeat", "2", "--seed", "41"],
                ),
            )

        build_mock.assert_called_once_with(
            ROOT, "debug", False, "ninja", targets=("draxul-test-core",)
        )
        self.assertEqual([str(executable), "[server]", "--list-tests"], capture.call_args.args[0])
        self.assertEqual(2, run_mock.call_count)
        self.assertIn("41", run_mock.call_args_list[0].args[0])
        self.assertIn("42", run_mock.call_args_list[1].args[0])

    def test_catch_inventory_count_accepts_filtered_and_unfiltered_output(self) -> None:
        self.assertEqual(3, draxul_do._catch_selection_count("3 matching test cases\n"))
        self.assertEqual(895, draxul_do._catch_selection_count("895 test cases\n"))

    def test_focused_target_rejects_zero_match_before_test_execution(self) -> None:
        executable = ROOT / "build" / "tests" / "draxul-test-core"
        errors = io.StringIO()
        with (
            contextlib.redirect_stderr(errors),
            mock.patch.object(
                draxul_do,
                "_configure_and_build",
                return_value=(0, ROOT / "build", "Debug", None),
            ),
            mock.patch.object(
                draxul_do, "_focused_test_executable", return_value=executable
            ),
            mock.patch.object(pathlib.Path, "is_file", return_value=True),
            mock.patch.object(
                draxul_do,
                "_capture_owned_process",
                return_value=(0, "0 matching test cases\n"),
            ),
            mock.patch.object(draxul_do, "run") as run_mock,
        ):
            self.assertEqual(
                2,
                draxul_do.cmd_test(
                    ROOT,
                    ["--target", "draxul-test-core", "--catch", "[missing]"],
                ),
            )

        run_mock.assert_not_called()
        self.assertIn("matched zero tests", errors.getvalue())


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
            mock.patch.object(
                draxul_do, "run_bounded_process_tree", return_value=0
            ) as run_mock,
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
            timeout_seconds=30,
        )

    def test_smoke_timeout_stops_the_owned_posix_process_group(self) -> None:
        process = mock.Mock(pid=4321)
        process.wait.side_effect = [
            subprocess.TimeoutExpired(["draxul"], 30),
            -signal.SIGTERM,
            -signal.SIGTERM,
        ]
        with (
            mock.patch.object(draxul_do.sys, "platform", "darwin"),
            mock.patch.object(
                draxul_do.subprocess, "Popen", return_value=process
            ) as popen,
            mock.patch.object(draxul_do.os, "killpg") as killpg,
        ):
            result = draxul_do.run_bounded_process_tree(
                ["draxul", "--smoke-test"],
                ROOT,
                timeout_seconds=30,
            )

        self.assertEqual(124, result)
        popen.assert_called_once_with(
            ["draxul", "--smoke-test"],
            cwd=ROOT,
            env=None,
            start_new_session=True,
        )
        killpg.assert_called_once_with(4321, signal.SIGTERM)

    def test_keyboard_interrupt_stops_the_owned_posix_process_group(self) -> None:
        process = mock.Mock(pid=4321)
        process.wait.side_effect = [KeyboardInterrupt, -signal.SIGTERM]
        with (
            mock.patch.object(draxul_do.sys, "platform", "darwin"),
            mock.patch.object(draxul_do.subprocess, "Popen", return_value=process),
            mock.patch.object(draxul_do.os, "killpg") as killpg,
        ):
            with self.assertRaises(KeyboardInterrupt):
                draxul_do.run(["cmake", "--build", "build"], ROOT)

        killpg.assert_called_once_with(4321, signal.SIGTERM)

    def test_windows_timeout_uses_new_group_and_taskkill_tree(self) -> None:
        process = mock.Mock(pid=9876)
        process.wait.side_effect = [
            subprocess.TimeoutExpired(["draxul"], 30),
            1,
        ]
        completed = subprocess.CompletedProcess([], 0)
        with (
            mock.patch.object(draxul_do.sys, "platform", "win32"),
            mock.patch.object(
                draxul_do.subprocess,
                "CREATE_NEW_PROCESS_GROUP",
                512,
                create=True,
            ),
            mock.patch.object(draxul_do.subprocess, "Popen", return_value=process) as popen,
            mock.patch.object(
                draxul_do.subprocess, "run", return_value=completed
            ) as run_mock,
        ):
            self.assertEqual(
                124,
                draxul_do.run_bounded_process_tree(
                    ["draxul.exe", "--smoke-test"], ROOT, timeout_seconds=30
                ),
            )

        self.assertEqual(512, popen.call_args.kwargs["creationflags"])
        self.assertEqual(
            ["taskkill", "/PID", "9876", "/T", "/F"],
            run_mock.call_args.args[0],
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


class FinalValidationCommandTests(unittest.TestCase):
    def test_failure_classification_distinguishes_build_tests_snapshots_and_environment(self) -> None:
        self.assertEqual("passed", draxul_do._validation_classification("ctest", 0))
        self.assertEqual("build failure", draxul_do._validation_classification("build", 1))
        self.assertEqual(
            "product-test failure",
            draxul_do._validation_classification("ctest", 1),
        )
        self.assertEqual(
            "snapshot failure",
            draxul_do._validation_classification("render", 1),
        )
        self.assertEqual(
            "validation-environment failure",
            draxul_do._validation_classification("smoke", 124),
        )

    def test_logged_step_discards_success_log_and_retains_complete_failure_log(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            log_dir = root / "logs"
            success = draxul_do._run_logged_validation_command(
                [sys.executable, "-c", "print('short success')"],
                root,
                step_name="success",
                kind="ctest",
                log_dir=log_dir,
            )
            failure = draxul_do._run_logged_validation_command(
                [
                    sys.executable,
                    "-c",
                    "import sys; print('first diagnostic'); "
                    "print('last diagnostic'); sys.exit(7)",
                ],
                root,
                step_name="failure",
                kind="ctest",
                log_dir=log_dir,
            )

            self.assertEqual(0, success.return_code)
            self.assertIsNone(success.log_path)
            self.assertEqual(7, failure.return_code)
            self.assertIsNotNone(failure.log_path)
            retained = failure.log_path.read_text(encoding="utf-8")
            self.assertIn("first diagnostic", retained)
            self.assertIn("last diagnostic", retained)

    def test_logged_step_classifies_command_start_error_as_environment_failure(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            result = draxul_do._run_logged_validation_command(
                [str(root / "missing-command")],
                root,
                step_name="missing-tool",
                kind="build",
                log_dir=root / "logs",
            )

            self.assertEqual(125, result.return_code)
            self.assertEqual("validation-environment failure", result.classification)
            self.assertIn(
                "could not start command",
                result.log_path.read_text(encoding="utf-8"),
            )

    def test_default_final_renders_are_core_platform_regressions(self) -> None:
        with mock.patch.object(draxul_do.sys, "platform", "darwin"):
            _, _, _, renders = draxul_do._parse_validate_args(ROOT, [])

        self.assertEqual(
            (
                "basic-view",
                "cmdline-view",
                "unicode-view",
                "panel-view",
                "nanovg-demo",
            ),
            renders,
        )
        self.assertNotIn("pcbview-plugin", renders)
        self.assertNotIn("rezonality-plugin", renders)

    def test_windows_command_seam_builds_once_then_runs_smoke_and_full_ctest(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            build_tree = root / "build-ninja-debug"
            executable = build_tree / "draxul.exe"
            executable.parent.mkdir(parents=True)
            executable.write_text("exe", encoding="utf-8")

            def completed_step(command, _cwd, *, step_name, kind, **_kwargs):
                return draxul_do.ValidationStepResult(
                    step_name, kind, 0, 0.25, detail=" ".join(command)
                )

            output = io.StringIO()
            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do.sys, "platform", "win32"),
                mock.patch.object(
                    draxul_do,
                    "_configure_and_build_impl",
                    return_value=(0, build_tree, "Debug", {"TEST": "1"}),
                ) as build_mock,
                mock.patch.object(draxul_do, "draxul_exe", return_value=executable),
                mock.patch.object(
                    draxul_do, "_ctest_selection_count", return_value=(0, 37, "")
                ),
                mock.patch.object(
                    draxul_do,
                    "_run_logged_validation_command",
                    side_effect=completed_step,
                ) as run_step,
            ):
                self.assertEqual(0, draxul_do.cmd_validate(root, ["--no-render"]))

            build_mock.assert_called_once()
            self.assertEqual(("draxul", "draxul-tests"), build_mock.call_args.kwargs["targets"])
            self.assertEqual(2, run_step.call_count)
            self.assertEqual("smoke", run_step.call_args_list[0].kwargs["step_name"])
            ctest_command = run_step.call_args_list[1].args[0]
            self.assertEqual("ctest", ctest_command[0])
            self.assertIn("--label-regex", ctest_command)
            self.assertIn("unit", ctest_command)
            self.assertIn("targets built: draxul, draxul-tests", output.getvalue())
            self.assertIn("tests selected: 37 CTest entries", output.getvalue())
            self.assertIn("steps: 3/3 passed", output.getvalue())

    def test_final_summary_reports_retained_failure_log_and_category(self) -> None:
        step = draxul_do.ValidationStepResult(
            "ctest-unit",
            "ctest",
            1,
            3.5,
            pathlib.Path("build/validation-logs/ctest-unit.log"),
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            draxul_do._print_final_validation_summary(
                targets=("draxul", "draxul-tests"),
                selected_tests=41,
                render_names=("panel-view",),
                steps=[step],
            )

        summary = output.getvalue()
        self.assertIn("product-test failure", summary)
        self.assertIn("41 CTest entries", summary)
        self.assertIn("panel-view", summary)
        self.assertIn("ctest-unit.log", summary)


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
