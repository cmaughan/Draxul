from __future__ import annotations

import contextlib
import importlib.util
import io
import pathlib
import stat
import sys
import tempfile
import unittest
import zipfile
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_do_module():
    spec = importlib.util.spec_from_file_location("draxul_do", ROOT / "do.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load do.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


draxul_do = load_do_module()


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

        self.assertIn("Run unit tests", help_output)
        self.assertNotIn("test         Run the full local test suite", help_output)

    def test_test_command_routes_posix_to_unit_script_mode(self) -> None:
        with (
            mock.patch.object(draxul_do.sys, "argv", ["do.py", "test"]),
            mock.patch.object(draxul_do.sys, "platform", "darwin"),
            mock.patch.object(draxul_do, "run", return_value=0) as run_mock,
        ):
            self.assertEqual(0, draxul_do.main())

        run_mock.assert_called_once_with(
            ["sh", "./scripts/run_tests.sh", "--unit"], ROOT
        )

    def test_test_command_routes_windows_to_unit_script_mode(self) -> None:
        with (
            mock.patch.object(draxul_do.sys, "argv", ["do.py", "test"]),
            mock.patch.object(draxul_do.sys, "platform", "win32"),
            mock.patch.object(draxul_do, "run", return_value=0) as run_mock,
        ):
            self.assertEqual(0, draxul_do.main())

        run_mock.assert_called_once_with(
            ["cmd", "/c", "t.bat", "--unit"], ROOT
        )


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


class ReviewArgumentParsingTests(unittest.TestCase):
    def test_review_defaults_to_all_with_default_timeout(self) -> None:
        parsed = draxul_do.parse_review_args([])

        self.assertEqual("all", parsed.review_target)
        self.assertEqual(900, parsed.agy_timeout_seconds)
        self.assertFalse(parsed.dry_run)

    def test_review_accepts_target_timeout_and_dry_run(self) -> None:
        parsed = draxul_do.parse_review_args(["gemini", "--agy-timeout", "1200", "--dry-run"])

        self.assertEqual("gemini", parsed.review_target)
        self.assertEqual(1200, parsed.agy_timeout_seconds)
        self.assertTrue(parsed.dry_run)

    def test_review_rejects_non_positive_timeout(self) -> None:
        with self.assertRaises(SystemExit):
            draxul_do.parse_review_args(["--agy-timeout", "0"])


class ReviewPlanTests(unittest.TestCase):
    def test_review_models_use_current_defaults(self) -> None:
        self.assertEqual("gpt-5.6-sol", draxul_do.CODEX_REVIEW_MODEL)
        self.assertEqual("fable", draxul_do.CLAUDE_REVIEW_MODEL)

    def test_default_review_plan_preserves_draxul_review_filenames(self) -> None:
        plan = draxul_do.create_review_plan(ROOT)

        self.assertEqual("All", plan.mode)
        self.assertEqual(ROOT / "plans" / "prompts" / "review.md", plan.review_prompt_path)
        self.assertEqual(ROOT / "plans" / "prompts" / "consensus_review.md", plan.consensus_prompt_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-latest.gpt.md", plan.codex_review_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-latest.gemini.md", plan.gemini_review_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-latest.claude.md", plan.claude_review_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-consensus.md", plan.consensus_review_path)

    def test_bug_review_plan_uses_bug_prompt_and_outputs(self) -> None:
        plan = draxul_do.create_review_plan(ROOT, prompt_stem="review_bugs", review_basename="review-bugs-latest")

        self.assertEqual(ROOT / "plans" / "prompts" / "review_bugs.md", plan.review_prompt_path)
        self.assertEqual(ROOT / "plans" / "prompts" / "consensus_review_bugs.md", plan.consensus_prompt_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-bugs-latest.gpt.md", plan.codex_review_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-bugs-latest.gemini.md", plan.gemini_review_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-bugs-latest.claude.md", plan.claude_review_path)
        self.assertEqual(ROOT / "plans" / "reviews" / "review-bugs-consensus.md", plan.consensus_review_path)

    def test_consensus_plan_sets_consensus_mode(self) -> None:
        plan = draxul_do.create_consensus_plan(ROOT, prompt_stem="review_bugs", review_basename="review-bugs-latest")

        self.assertEqual("Consensus", plan.mode)
        self.assertEqual(ROOT / "plans" / "prompts" / "consensus_review_bugs.md", plan.consensus_prompt_path)


class NativeCommandTests(unittest.TestCase):
    def test_native_command_sends_stdin_as_utf8(self) -> None:
        command = [
            sys.executable,
            "-c",
            "import sys; data=sys.stdin.buffer.read(); sys.stdout.buffer.write(data)",
        ]

        result = draxul_do.run_native_command(command, ROOT, input_text="snowman: \u2603")

        self.assertEqual(0, result.returncode)
        self.assertEqual("snowman: \u2603", result.stdout)

    def test_native_command_decodes_non_utf8_output_with_replacement(self) -> None:
        command = [
            sys.executable,
            "-c",
            "import sys; sys.stdout.buffer.write(b'bad: \\xff')",
        ]

        result = draxul_do.run_native_command(command, ROOT)

        self.assertEqual(0, result.returncode)
        self.assertEqual("bad: \ufffd", result.stdout)


if __name__ == "__main__":
    unittest.main()
