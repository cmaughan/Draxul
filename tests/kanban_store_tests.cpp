#include <catch2/catch_all.hpp>

#include <draxul/kanban/kanban_store.h>

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

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
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

    REQUIRE(reorder_card(board, KanbanSelection{.column = 0, .card = 1}, -1, &error));
    REQUIRE(save_kanban_order(board, &error));

    REQUIRE(std::filesystem::exists(root / "pending" / "a.md"));
    REQUIRE(std::filesystem::exists(root / "pending" / "b.md"));
    REQUIRE(board.columns[0].cards[0].file_name == "b.md");

    const auto metadata = read_file(root / std::string(kKanbanMetadataFileName));
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
        .cards = {KanbanCard{.file_name = "quote\"slash\\card.md"}},
    });

    std::string error;
    REQUIRE(save_kanban_order(board, &error));

    const auto metadata = read_file(root / std::string(kKanbanMetadataFileName));
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
            KanbanCard{.file_name = "a.md"},
            KanbanCard{.file_name = "b.md"},
            KanbanCard{.file_name = "c.md"},
        },
    });

    std::string error;
    REQUIRE(reorder_card(
        board,
        KanbanSelection{.column = 0, .card = 1},
        std::numeric_limits<int>::max(),
        &error));
    REQUIRE(board.columns[0].cards[2].file_name == "b.md");

    REQUIRE(reorder_card(
        board,
        KanbanSelection{.column = 0, .card = 1},
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

    REQUIRE(move_card_to_column(board, KanbanSelection{.column = 0, .card = 0}, 1, &error));

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

    REQUIRE_FALSE(move_card_to_column(board, KanbanSelection{.column = 0, .card = 0}, 1, &error));

    REQUIRE_FALSE(error.empty());
    REQUIRE(std::filesystem::exists(root / "pending" / "a.md"));
    REQUIRE(std::filesystem::exists(root / "done" / "a.md"));
    REQUIRE(board.columns[0].cards.size() == 1);
    REQUIRE(board.columns[1].cards.size() == 1);
}
