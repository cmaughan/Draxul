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
    def test_graphify_parser_is_consumed_for_megacity(self) -> None:
        app_args, parser = draxul_do._consume_megacity_parser_args(
            ["--host", "megacity", "--parser", "graphify", "--console"]
        )

        self.assertEqual(["--host", "megacity", "--console"], app_args)
        self.assertEqual("graphify", parser)

    def test_treesitter_parser_accepts_alias_and_normalizes(self) -> None:
        app_args, parser = draxul_do._consume_megacity_parser_args(
            ["--host", "megacity", "--parser", "treesitter"]
        )

        self.assertEqual(["--host", "megacity"], app_args)
        self.assertEqual("treesitter_db", parser)

    def test_parser_requires_megacity_host(self) -> None:
        with self.assertRaisesRegex(ValueError, "--parser is only supported with --host megacity"):
            draxul_do._consume_megacity_parser_args(["--host", "nvim", "--parser", "graphify"])

    def test_parser_requires_value(self) -> None:
        with self.assertRaisesRegex(ValueError, "--parser requires a value"):
            draxul_do._consume_megacity_parser_args(["--host", "megacity", "--parser"])


class MegacityConfigMergeTests(unittest.TestCase):
    def test_graphify_parser_appends_missing_section(self) -> None:
        merged = draxul_do._merge_megacity_parser_config("font_size = 14\n", "graphify")

        self.assertEqual(
            'font_size = 14\n\n[mega_city_code]\ncode_source = "graphify"\ngraphify_graph_path = "graphify-out/graph.json"\n',
            merged,
        )

    def test_graphify_parser_updates_existing_section_and_preserves_other_keys(self) -> None:
        merged = draxul_do._merge_megacity_parser_config(
            'font_size = 14\n\n[mega_city_code]\nshow_ui_panels = true\ncode_source = "treesitter_db"\n\n[terminal]\nfg = "#fff"\n',
            "graphify",
        )

        self.assertIn('[mega_city_code]\nshow_ui_panels = true\ncode_source = "graphify"\ngraphify_graph_path = "graphify-out/graph.json"\n\n[terminal]', merged)
        self.assertIn('fg = "#fff"', merged)

    def test_treesitter_parser_updates_only_code_source(self) -> None:
        merged = draxul_do._merge_megacity_parser_config(
            '[mega_city_code]\ncode_source = "graphify"\ngraphify_graph_path = "graphify-out/graph.json"\n',
            "treesitter_db",
        )

        self.assertEqual(
            '[mega_city_code]\ncode_source = "treesitter_db"\ngraphify_graph_path = "graphify-out/graph.json"\n',
            merged,
        )


if __name__ == "__main__":
    unittest.main()
