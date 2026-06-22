from __future__ import annotations

import importlib.util
import pathlib
import unittest


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


if __name__ == "__main__":
    unittest.main()
