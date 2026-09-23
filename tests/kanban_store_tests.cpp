#include <catch2/catch_all.hpp>

#include <draxul/kanban/kanban_store.h>

#include "support/kanban_directory_scan_test_support.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

using namespace draxul::kanban;

namespace
{
void write_file(const std::filesystem::path& path, std::string_view text = "")
{
    std::ofstream file(path, std::ios::binary);
    file << text;
}

} // namespace

TEST_CASE("kanban store resolves empty source to working directory kanban root", "[kanban][store]")
{
    draxul::tests::TempDir temp("draxul-kanban-resolve");

    std::string error;
    const auto root = resolve_kanban_root({}, temp.path, &error);

    REQUIRE(error.empty());
    REQUIRE(std::filesystem::exists(root));
    REQUIRE(std::filesystem::is_directory(root));
    REQUIRE(root.filename() == "kanban");
    REQUIRE(std::filesystem::equivalent(root.parent_path(), temp.path));
    REQUIRE(std::filesystem::is_directory(root / "ice-box"));
    REQUIRE(std::filesystem::is_directory(root / "pending"));
    REQUIRE(std::filesystem::is_directory(root / "done"));
}

TEST_CASE("kanban store scans folders and markdown files", "[kanban][store]")
{
    draxul::tests::TempDir temp("draxul-kanban-scan");
    const auto root = temp.path / "kanban";
    std::filesystem::create_directories(root / "pending");
    std::filesystem::create_directories(root / "done");
    std::filesystem::create_directories(root / ".hidden");
    std::filesystem::create_directories(root / "pending" / "nested");
    write_file(root / "pending" / "b-bug.md");
    write_file(root / "pending" / "a-feature.md");
    write_file(root / "pending" / "ignored.txt");
    write_file(root / "pending" / "nested" / "nested.md");
    write_file(root / ".hidden" / "secret.md");

    std::string error;
    const auto board = load_kanban_board(root, &error);

    REQUIRE(error.empty());
    REQUIRE(board.root == root);
    REQUIRE(board.columns.size() == 2);
    REQUIRE(board.columns[0].name == "pending");
    REQUIRE(board.columns[0].cards.size() == 2);
    REQUIRE(board.columns[0].cards[0].file_name == "a-feature.md");
    REQUIRE(board.columns[0].cards[0].kind == CardKind::Feature);
    REQUIRE(board.columns[0].cards[1].file_name == "b-bug.md");
    REQUIRE(board.columns[1].name == "done");
}

TEST_CASE("kanban store reports root and column iterator failures",
    "[kanban][store][scan-error]")
{
    draxul::tests::TempDir temp("draxul-kanban-scan-errors");
    const auto root = temp.path / "kanban";
    const auto column = root / "pending";
    std::filesystem::create_directories(column);
    write_file(column / "existing.md");

    const auto verify_failure
        = [&](const std::filesystem::path& directory,
              draxul::tests::KanbanScanFailurePoint point,
              std::string_view expected_scope) {
              draxul::tests::FaultInjectingKanbanDirectoryOperations
                  operations(directory, point);
              ScopedKanbanDirectoryOperationsOverride override(
                  operations);
              std::string error;
              KanbanBoard board;
              CHECK_NOTHROW(board = load_kanban_board(root, &error));
              CHECK(error.find(expected_scope) != std::string::npos);
              CHECK_FALSE(error.empty());
              CHECK(board.root == root);
              CHECK(operations.injected_failures == 1);
          };

    SECTION("root iterator construction")
    {
        verify_failure(root,
            draxul::tests::KanbanScanFailurePoint::Construction,
            "failed to scan kanban root");
    }
    SECTION("root iterator advancement")
    {
        verify_failure(root,
            draxul::tests::KanbanScanFailurePoint::Advancement,
            "failed to scan kanban root");
    }
    SECTION("column iterator construction")
    {
        verify_failure(column,
            draxul::tests::KanbanScanFailurePoint::Construction,
            "failed to scan kanban column");
    }
    SECTION("column iterator advancement")
    {
        verify_failure(column,
            draxul::tests::KanbanScanFailurePoint::Advancement,
            "failed to scan kanban column");
    }
}

TEST_CASE("kanban store merges metadata order with discovered entries", "[kanban][store]")
{
    draxul::tests::TempDir temp("draxul-kanban-metadata");
    const auto root = temp.path / "kanban";
    std::filesystem::create_directories(root / "pending");
    std::filesystem::create_directories(root / "done");
    std::filesystem::create_directories(root / "review");
    write_file(root / "pending" / "a-feature.md");
    write_file(root / "pending" / "b-bug.md");
    write_file(root / "done" / "done.md");
    write_file(root / "review" / "new.md");
    write_file(root / std::string(kKanbanMetadataFileName),
        "version = 1\n"
        "columns = [\"done\", \"pending\", \"missing\"]\n"
        "[cards]\n"
        "pending = [\"b-bug.md\", \"missing.md\", \"a-feature.md\"]\n");

    std::string error;
    const auto board = load_kanban_board(root, &error);

    REQUIRE(error.empty());
    REQUIRE(board.columns.size() == 3);
    REQUIRE(board.columns[0].name == "done");
    REQUIRE(board.columns[1].name == "pending");
    REQUIRE(board.columns[2].name == "review");
    REQUIRE(board.columns[1].cards[0].file_name == "b-bug.md");
    REQUIRE(board.columns[1].cards[1].file_name == "a-feature.md");
}

TEST_CASE("kanban store persists reordered cards without moving files", "[kanban][store]")
{
    draxul::tests::TempDir temp("draxul-kanban-reorder");
    const auto root = temp.path / "kanban";
    std::filesystem::create_directories(root / "pending");
    write_file(root / "pending" / "a.md", "a");
    write_file(root / "pending" / "b.md", "b");

    std::string error;
    auto board = load_kanban_board(root, &error);
    REQUIRE(error.empty());

    REQUIRE(reorder_card(board, KanbanSelection{ .column = 0, .card = 1 }, -1, &error));
    REQUIRE(save_kanban_order(board, &error));

    REQUIRE(std::filesystem::exists(root / "pending" / "a.md"));
    REQUIRE(std::filesystem::exists(root / "pending" / "b.md"));
    REQUIRE(board.columns[0].cards[0].file_name == "b.md");

    const auto metadata = draxul::tests::read_file(root / std::string(kKanbanMetadataFileName));
    REQUIRE_THAT(metadata, Catch::Matchers::ContainsSubstring("version = 1\n"));
    REQUIRE_THAT(metadata, Catch::Matchers::ContainsSubstring("columns = [\"pending\"]\n"));
    REQUIRE_THAT(metadata, Catch::Matchers::ContainsSubstring("[cards]\n"));
    REQUIRE_THAT(metadata, Catch::Matchers::ContainsSubstring("pending = [\"b.md\", \"a.md\"]\n"));

    const auto reloaded = load_kanban_board(root, &error);
    REQUIRE(error.empty());
    REQUIRE(reloaded.columns[0].cards[0].file_name == "b.md");
    REQUIRE(reloaded.columns[0].cards[1].file_name == "a.md");
}

TEST_CASE("kanban store escapes metadata strings when saving", "[kanban][store]")
{
    draxul::tests::TempDir temp("draxul-kanban-escape-save");
    const auto root = temp.path / "kanban";
    std::filesystem::create_directories(root);
    write_file(root / std::string(kKanbanMetadataFileName), "stale metadata\n");

    KanbanBoard board;
    board.root = root;
    board.columns.push_back(KanbanColumn{
        .name = "pending",
        .cards = { KanbanCard{ .file_name = "quote\"slash\\card.md" } },
    });

    std::string error;
    REQUIRE(save_kanban_order(board, &error));

    const auto metadata = draxul::tests::read_file(root / std::string(kKanbanMetadataFileName));
    REQUIRE_THAT(metadata, Catch::Matchers::ContainsSubstring("columns = [\"pending\"]\n"));
    REQUIRE_THAT(
        metadata,
        Catch::Matchers::ContainsSubstring("pending = [\"quote\\\"slash\\\\card.md\"]\n"));
    REQUIRE_FALSE(std::filesystem::exists(root / ".draxul-kanban.toml.tmp"));
    REQUIRE_FALSE(std::filesystem::exists(root / ".draxul-kanban.toml.bak"));
}

TEST_CASE("kanban store clamps card reorder with extreme row deltas", "[kanban][store]")
{
    KanbanBoard board;
    board.columns.push_back(KanbanColumn{
        .name = "pending",
        .cards = {
            KanbanCard{ .file_name = "a.md" },
            KanbanCard{ .file_name = "b.md" },
            KanbanCard{ .file_name = "c.md" },
        },
    });

    std::string error;
    REQUIRE(reorder_card(
        board,
        KanbanSelection{ .column = 0, .card = 1 },
        std::numeric_limits<int>::max(),
        &error));
    REQUIRE(board.columns[0].cards[2].file_name == "b.md");

    REQUIRE(reorder_card(
        board,
        KanbanSelection{ .column = 0, .card = 1 },
        std::numeric_limits<int>::min(),
        &error));
    REQUIRE(board.columns[0].cards[0].file_name == "c.md");
}

TEST_CASE("kanban store moves cards across columns by renaming files", "[kanban][store]")
{
    draxul::tests::TempDir temp("draxul-kanban-move");
    const auto root = temp.path / "kanban";
    std::filesystem::create_directories(root / "pending");
    std::filesystem::create_directories(root / "done");
    write_file(root / "pending" / "a.md", "card");
    write_file(root / "done" / "z.md", "done");

    std::string error;
    auto board = load_kanban_board(root, &error);
    REQUIRE(error.empty());

    REQUIRE(move_card_to_column(board, KanbanSelection{ .column = 0, .card = 0 }, 1, &error));

    REQUIRE_FALSE(std::filesystem::exists(root / "pending" / "a.md"));
    REQUIRE(std::filesystem::exists(root / "done" / "a.md"));
    REQUIRE(board.columns[0].cards.empty());
    REQUIRE(board.columns[1].cards[0].file_name == "a.md");
    REQUIRE(board.columns[1].cards[0].path == root / "done" / "a.md");
}

TEST_CASE("kanban store refuses cross-column filename collisions", "[kanban][store]")
{
    draxul::tests::TempDir temp("draxul-kanban-collision");
    const auto root = temp.path / "kanban";
    std::filesystem::create_directories(root / "pending");
    std::filesystem::create_directories(root / "done");
    write_file(root / "pending" / "a.md", "source");
    write_file(root / "done" / "a.md", "destination");

    std::string error;
    auto board = load_kanban_board(root, &error);
    REQUIRE(error.empty());

    REQUIRE_FALSE(move_card_to_column(board, KanbanSelection{ .column = 0, .card = 0 }, 1, &error));

    REQUIRE_FALSE(error.empty());
    REQUIRE(std::filesystem::exists(root / "pending" / "a.md"));
    REQUIRE(std::filesystem::exists(root / "done" / "a.md"));
    REQUIRE(board.columns[0].cards.size() == 1);
    REQUIRE(board.columns[1].cards.size() == 1);
}

TEST_CASE("kanban store deletes only the selected workspace card",
    "[kanban][store][workspace][mutation]")
{
    draxul::tests::TempDir temp("draxul-kanban-workspace-delete");
    const auto workspace = temp.path / "workspace";
    const auto root = workspace / "kanban";
    const auto product_root = workspace / "plugins" / "product" / "kanban";
    std::filesystem::create_directories(root / "done");
    std::filesystem::create_directories(product_root / "done");
    write_file(root / "done" / "same-feature.md", "root done");
    write_file(product_root / "done" / "same-feature.md", "product done");
    write_file(workspace / ".gitmodules",
        "[submodule \"plugins/product\"]\n"
        "  path = plugins/product\n");

    std::string error;
    auto board = load_kanban_workspace(root, &error);
    REQUIRE(error.empty());
    REQUIRE(board.columns.size() == 1);
    REQUIRE(board.columns[0].name == "done");
    REQUIRE(board.columns[0].cards.size() == 2);
    REQUIRE(board.columns[0].cards[1].source_name == "product");

    REQUIRE(delete_card(
        board,
        KanbanSelection{ .column = 0, .card = 1 },
        &error));
    CHECK(std::filesystem::exists(root / "done" / "same-feature.md"));
    CHECK_FALSE(std::filesystem::exists(product_root / "done" / "same-feature.md"));
    REQUIRE(board.columns[0].cards.size() == 1);
    CHECK(board.columns[0].cards[0].source_name == "workspace");

    REQUIRE(save_kanban_order_for_source(board, 1, &error));
    const auto metadata = draxul::tests::read_file(
        product_root / std::string(kKanbanMetadataFileName));
    CHECK_THAT(metadata, Catch::Matchers::ContainsSubstring("done = []"));
}

TEST_CASE("kanban workspace merges initialized recursive submodule boards",
    "[kanban][store][workspace]")
{
    draxul::tests::TempDir temp("draxul-kanban-workspace");
    const auto workspace = temp.path / "workspace";
    const auto root = workspace / "kanban";
    const auto megacity_root = workspace / "plugins" / "megacity" / "kanban";
    const auto nested_root = workspace / "plugins" / "megacity" / "vendor" / "notes" / "kanban";
    std::filesystem::create_directories(root / "pending");
    std::filesystem::create_directories(root / "done");
    std::filesystem::create_directories(megacity_root / "pending");
    std::filesystem::create_directories(nested_root / "pending");
    write_file(root / "pending" / "same-feature.md");
    write_file(megacity_root / "pending" / "same-feature.md");
    write_file(nested_root / "pending" / "nested-bug.md");
    write_file(workspace / ".gitmodules",
        "[submodule \"plugins/megacity\"]\n"
        "  path = plugins/megacity\n"
        "[submodule \"plugins/uninitialized\"]\n"
        "  path = plugins/uninitialized\n");
    write_file(workspace / "plugins" / "megacity" / ".gitmodules",
        "[submodule \"vendor/notes\"]\n"
        "  path = vendor/notes\n");

    std::string error;
    const auto board = load_kanban_workspace(root, &error);

    REQUIRE(error.empty());
    REQUIRE(board.warnings.empty());
    REQUIRE(board.sources.size() == 3);
    REQUIRE(board.sources[0].name == "workspace");
    REQUIRE(board.sources[1].name == "megacity");
    REQUIRE(board.sources[2].name == "notes");
    REQUIRE(board.columns.size() == 2);
    REQUIRE(board.columns[0].name == "pending");
    REQUIRE(board.columns[0].cards.size() == 3);
    CHECK(board.columns[0].cards[0].source_name == "workspace");
    CHECK(board.columns[0].cards[1].source_name == "megacity");
    CHECK(board.columns[0].cards[2].source_name == "notes");
    CHECK(board.columns[0].cards[0].file_name == "same-feature.md");
    CHECK(board.columns[0].cards[1].file_name == "same-feature.md");
    CHECK(board.columns[0].cards[0].path != board.columns[0].cards[1].path);
}

TEST_CASE("kanban workspace moves and orders cards within their owning board",
    "[kanban][store][workspace][mutation]")
{
    draxul::tests::TempDir temp("draxul-kanban-workspace-move");
    const auto workspace = temp.path / "workspace";
    const auto root = workspace / "kanban";
    const auto product_root = workspace / "plugins" / "product" / "kanban";
    std::filesystem::create_directories(root / "pending");
    std::filesystem::create_directories(root / "done");
    std::filesystem::create_directories(product_root / "pending");
    std::filesystem::create_directories(product_root / "done");
    write_file(root / "pending" / "same-feature.md", "root");
    write_file(root / "done" / "same-feature.md", "root done");
    write_file(product_root / "pending" / "a-feature.md", "product a");
    write_file(product_root / "pending" / "same-feature.md", "product same");
    write_file(workspace / ".gitmodules",
        "[submodule \"plugins/product\"]\n"
        "  path = plugins/product\n");

    std::string error;
    auto board = load_kanban_workspace(root, &error);
    REQUIRE(error.empty());
    REQUIRE(board.sources.size() == 2);
    REQUIRE(board.columns[0].name == "pending");
    REQUIRE(board.columns[1].name == "done");

    REQUIRE(reorder_card(
        board,
        KanbanSelection{ .column = 0, .card = 2 },
        -1,
        &error));
    CHECK(board.columns[0].cards[0].source_name == "workspace");
    CHECK(board.columns[0].cards[1].file_name == "same-feature.md");
    CHECK(board.columns[0].cards[1].source_name == "product");
    CHECK(board.columns[0].cards[2].file_name == "a-feature.md");

    REQUIRE(move_card_to_column(
        board,
        KanbanSelection{ .column = 0, .card = 1 },
        1,
        &error));
    CHECK(std::filesystem::exists(root / "pending" / "same-feature.md"));
    CHECK(std::filesystem::exists(root / "done" / "same-feature.md"));
    CHECK_FALSE(std::filesystem::exists(product_root / "pending" / "same-feature.md"));
    CHECK(std::filesystem::exists(product_root / "done" / "same-feature.md"));

    REQUIRE(save_kanban_order_for_source(board, 1, &error));
    CHECK_FALSE(std::filesystem::exists(root / std::string(kKanbanMetadataFileName)));
    const auto metadata = draxul::tests::read_file(
        product_root / std::string(kKanbanMetadataFileName));
    CHECK_THAT(metadata,
        Catch::Matchers::ContainsSubstring("pending = [\"a-feature.md\"]"));
    CHECK_THAT(metadata,
        Catch::Matchers::ContainsSubstring("done = [\"same-feature.md\"]"));
}

TEST_CASE("kanban workspace keeps healthy boards when a submodule scan fails",
    "[kanban][store][workspace][scan-error]")
{
    draxul::tests::TempDir temp("draxul-kanban-workspace-scan-error");
    const auto workspace = temp.path / "workspace";
    const auto root = workspace / "kanban";
    const auto product_root = workspace / "plugins" / "product" / "kanban";
    std::filesystem::create_directories(root / "pending");
    std::filesystem::create_directories(product_root / "pending");
    write_file(root / "pending" / "root-feature.md");
    write_file(product_root / "pending" / "product-feature.md");
    write_file(workspace / ".gitmodules",
        "[submodule \"plugins/product\"]\n"
        "  path = plugins/product\n");

    draxul::tests::FaultInjectingKanbanDirectoryOperations operations(
        product_root / "pending",
        draxul::tests::KanbanScanFailurePoint::Construction);
    std::string error;
    KanbanBoard board;
    {
        ScopedKanbanDirectoryOperationsOverride override(operations);
        board = load_kanban_workspace(root, &error);
    }

    CHECK(error.empty());
    REQUIRE(board.sources.size() == 1);
    REQUIRE(board.warnings.size() == 1);
    CHECK(board.columns[0].cards.size() == 1);
    CHECK(board.columns[0].cards[0].file_name == "root-feature.md");
    CHECK_THAT(board.warnings[0], Catch::Matchers::ContainsSubstring("product"));
}
