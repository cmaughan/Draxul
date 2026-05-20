# Kanban Viewer Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native Draxul Kanban viewer host that scans a local `kanban/` folder, renders folder columns with Markdown-file cards, supports Vim-style navigation and card movement, and opens cards in the native Markdown viewer.

**Architecture:** Build a new grid-backed host that uses the existing terminal font pipeline and `GridHostBase`, not the Markdown rich-text renderer. Keep filesystem/order logic in a small testable Kanban library, let the host only render and translate input into model/store operations, and add one narrow app callback so hosts can request a Markdown pane without depending on `HostManager`.

**Tech Stack:** C++20, `std::filesystem`, small local TOML-compatible metadata parsing/writing for the board order file, `draxul-host` grid rendering, `draxul-types` Unicode helpers, Catch2, existing split-pane app plumbing.

---

## Product Scope

- Launch with `--host kanban`; `--source <folder>` overrides the board root.
- If `--source` is empty, resolve the board root as `<working_dir>/kanban`; if `working_dir` is empty, use the process current directory plus `kanban`.
- A board root contains one child folder per column. Hidden folders and metadata files are ignored.
- Each `.md` file directly under a column folder becomes a card.
- The board order is saved in `kanban/.draxul-kanban.toml`.
- `j/k/h/l` and arrow keys move the active selection.
- `Shift+j/k` reorder cards within the current column and persist metadata.
- `Shift+h/l` move the selected Markdown file to the adjacent column folder, update the in-memory board, and persist metadata.
- `Enter` opens the active card in an existing Markdown host if one exists; otherwise it creates a vertical split with a Markdown host for that file.
- Card icons are based on filename suffix before `.md`:
  - `*-bug.md` uses bug icon bytes `\xF0\x9F\x90\x9B`
  - `*-feature.md` uses sparkle icon bytes `\xE2\x9C\xA8`
  - `*-refactor.md` uses wrench icon bytes `\xF0\x9F\x94\xA7`
  - all other Markdown files use note icon bytes `\xF0\x9F\x93\x84`
- Rendering is clean, terminal-font based, and stable at any grid size: column widths are equal, cards are single-line with ellipsis truncation, the active card row has a background highlight, and a status line shows the root and selected file.

## File Structure

- Create `libs/draxul-kanban/CMakeLists.txt`
  - Defines `draxul-kanban`.
- Create `libs/draxul-kanban/include/draxul/kanban/kanban_board.h`
  - Pure model structs and suffix/icon helpers.
- Create `libs/draxul-kanban/src/kanban_board.cpp`
  - Card kind classification, default column ordering, selection clamp helpers.
- Create `libs/draxul-kanban/include/draxul/kanban/kanban_store.h`
  - Public filesystem/metadata load, save, reorder, and move APIs.
- Create `libs/draxul-kanban/src/kanban_store.cpp`
  - TOML metadata parser/writer and file move logic.
- Create `libs/draxul-kanban/include/draxul/kanban/kanban_navigation.h`
  - Key-to-command mapper.
- Create `libs/draxul-kanban/src/kanban_navigation.cpp`
  - SDL key handling for Vim and arrow keys.
- Create `libs/draxul-kanban/include/draxul/kanban/kanban_layout.h`
  - Grid layout structs, hit rows, and cell text helpers.
- Create `libs/draxul-kanban/src/kanban_layout.cpp`
  - Equal-width column layout, truncation, display-cluster writing.
- Create `libs/draxul-kanban/include/draxul/kanban/kanban_host.h`
  - `GridHostBase` host declaration and provider registration.
- Create `libs/draxul-kanban/src/kanban_host.cpp`
  - Host init, rendering, input handling, scroll-to-selection, status text.
- Modify `libs/draxul-types/include/draxul/host_kind.h`
  - Add `HostKind::Kanban`, parse aliases `kanban`/`kb`, stringify as `kanban`.
- Modify `libs/draxul-host/include/draxul/host.h`
  - Add `IHostCallbacks::open_markdown_source()` and `IHost::is_markdown_host()`.
- Modify `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
  - Override `is_markdown_host()`.
- Modify `libs/draxul-markdown/src/markdown_host.cpp`
  - Handle `open_file:<path>` and return true from `is_markdown_host()`.
- Modify `app/app.h` and `app/app.cpp`
  - Implement `open_markdown_source()` by reusing a Markdown pane or vertical-splitting a new one.
- Modify `app/main.cpp`
  - Register the Kanban host provider.
- Modify `app/command_palette.cpp`
  - Include `Kanban` in host-kind actions. Markdown is intentionally omitted there because it needs a concrete source file path.
- Modify root `CMakeLists.txt` and `tests/CMakeLists.txt`
  - Add `draxul-kanban` to build, tests, sanitizers, and app link.
- Create `tests/kanban_board_tests.cpp`
  - Model/icon/default ordering tests.
- Create `tests/kanban_store_tests.cpp`
  - Filesystem scan, metadata merge, reorder, and file move tests.
- Create `tests/kanban_navigation_tests.cpp`
  - Key mapping tests.
- Create `tests/kanban_layout_tests.cpp`
  - Equal column, active row, truncation, and scroll offset tests.
- Modify `docs/features.md`
  - Document `--host kanban`, metadata file, keybindings, and Markdown split behavior.

## Agent Ownership

Use these boundaries if implementing with subagents:

- Agent A owns `libs/draxul-kanban/include/draxul/kanban/kanban_board.h`, `libs/draxul-kanban/src/kanban_board.cpp`, `libs/draxul-kanban/include/draxul/kanban/kanban_store.h`, `libs/draxul-kanban/src/kanban_store.cpp`, `tests/kanban_board_tests.cpp`, and `tests/kanban_store_tests.cpp`.
- Agent B owns `libs/draxul-kanban/include/draxul/kanban/kanban_navigation.h`, `libs/draxul-kanban/src/kanban_navigation.cpp`, `libs/draxul-kanban/include/draxul/kanban/kanban_layout.h`, `libs/draxul-kanban/src/kanban_layout.cpp`, `tests/kanban_navigation_tests.cpp`, and `tests/kanban_layout_tests.cpp`.
- Agent C owns `libs/draxul-host/include/draxul/host.h`, `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`, `libs/draxul-markdown/src/markdown_host.cpp`, `app/app.h`, and `app/app.cpp`.
- Main integrator owns `HostKind`, `KanbanHost`, CMake, provider registration, command palette, docs, and final validation.

---

### Task 1: Kanban Model

**Files:**
- Create: `libs/draxul-kanban/include/draxul/kanban/kanban_board.h`
- Create: `libs/draxul-kanban/src/kanban_board.cpp`
- Create: `tests/kanban_board_tests.cpp`

- [ ] **Step 1: Write model tests**

Add `tests/kanban_board_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <draxul/kanban/kanban_board.h>

using namespace draxul::kanban;

TEST_CASE("kanban card kind is inferred from markdown filename suffix", "[kanban][board]")
{
    REQUIRE(card_kind_for_file("gpu-crash-bug.md") == CardKind::Bug);
    REQUIRE(card_kind_for_file("markdown-preview-feature.md") == CardKind::Feature);
    REQUIRE(card_kind_for_file("renderer-cleanup-refactor.md") == CardKind::Refactor);
    REQUIRE(card_kind_for_file("notes.md") == CardKind::Note);
    REQUIRE(card_kind_for_file("notes.txt") == CardKind::Note);
}

TEST_CASE("kanban icon strings are stable utf8 byte clusters", "[kanban][board]")
{
    REQUIRE(icon_for_kind(CardKind::Bug) == std::string("\xF0\x9F\x90\x9B"));
    REQUIRE(icon_for_kind(CardKind::Feature) == std::string("\xE2\x9C\xA8"));
    REQUIRE(icon_for_kind(CardKind::Refactor) == std::string("\xF0\x9F\x94\xA7"));
    REQUIRE(icon_for_kind(CardKind::Note) == std::string("\xF0\x9F\x93\x84"));
}

TEST_CASE("kanban default column order prefers agent work stages", "[kanban][board]")
{
    std::vector<std::string> names{ "done", "pending", "ice-box", "review" };
    sort_columns_for_first_load(names);
    REQUIRE(names == std::vector<std::string>{ "ice-box", "pending", "review", "done" });
}

TEST_CASE("kanban selection clamps to existing columns and cards", "[kanban][board]")
{
    KanbanBoard board;
    board.columns.push_back(KanbanColumn{ .name = "pending" });
    board.columns.push_back(KanbanColumn{ .name = "done" });
    board.columns[0].cards.push_back(KanbanCard{ .file_name = "a-feature.md" });
    board.columns[0].cards.push_back(KanbanCard{ .file_name = "b-bug.md" });

    KanbanSelection selection{ .column = 9, .card = 9 };
    clamp_selection(board, selection);
    REQUIRE(selection.column == 1);
    REQUIRE(selection.card == 0);

    selection = KanbanSelection{ .column = 0, .card = 9 };
    clamp_selection(board, selection);
    REQUIRE(selection.column == 0);
    REQUIRE(selection.card == 1);
}
```

- [ ] **Step 2: Run the failing model tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][board]"
```

Expected: build fails because the Kanban headers do not exist.

- [ ] **Step 3: Add the model header**

Add `libs/draxul-kanban/include/draxul/kanban/kanban_board.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::kanban
{

enum class CardKind
{
    Note,
    Bug,
    Feature,
    Refactor,
};

struct KanbanCard
{
    std::string file_name;
    std::filesystem::path path;
    CardKind kind = CardKind::Note;
};

struct KanbanColumn
{
    std::string name;
    std::filesystem::path directory;
    std::vector<KanbanCard> cards;
};

struct KanbanBoard
{
    std::filesystem::path root;
    std::vector<KanbanColumn> columns;
};

struct KanbanSelection
{
    int column = 0;
    int card = 0;
};

CardKind card_kind_for_file(std::string_view file_name);
std::string icon_for_kind(CardKind kind);
void sort_columns_for_first_load(std::vector<std::string>& names);
void clamp_selection(const KanbanBoard& board, KanbanSelection& selection);
bool selection_has_card(const KanbanBoard& board, KanbanSelection selection);
KanbanCard* selected_card(KanbanBoard& board, KanbanSelection selection);
const KanbanCard* selected_card(const KanbanBoard& board, KanbanSelection selection);

} // namespace draxul::kanban
```

- [ ] **Step 4: Add the model implementation**

Add `libs/draxul-kanban/src/kanban_board.cpp`:

```cpp
#include <draxul/kanban/kanban_board.h>

#include <algorithm>
#include <array>

namespace draxul::kanban
{
namespace
{

bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size()
        && text.substr(text.size() - suffix.size()) == suffix;
}

int preferred_column_rank(std::string_view name)
{
    constexpr std::array<std::string_view, 8> order = {
        "ice-box",
        "backlog",
        "pending",
        "todo",
        "doing",
        "in-progress",
        "review",
        "done",
    };
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (name == order[i])
            return static_cast<int>(i);
    }
    return static_cast<int>(order.size());
}

} // namespace

CardKind card_kind_for_file(std::string_view file_name)
{
    if (ends_with(file_name, "-bug.md"))
        return CardKind::Bug;
    if (ends_with(file_name, "-feature.md"))
        return CardKind::Feature;
    if (ends_with(file_name, "-refactor.md"))
        return CardKind::Refactor;
    return CardKind::Note;
}

std::string icon_for_kind(CardKind kind)
{
    switch (kind)
    {
    case CardKind::Bug:
        return "\xF0\x9F\x90\x9B";
    case CardKind::Feature:
        return "\xE2\x9C\xA8";
    case CardKind::Refactor:
        return "\xF0\x9F\x94\xA7";
    case CardKind::Note:
        return "\xF0\x9F\x93\x84";
    }
    return "\xF0\x9F\x93\x84";
}

void sort_columns_for_first_load(std::vector<std::string>& names)
{
    std::ranges::sort(names, [](const std::string& lhs, const std::string& rhs) {
        const int lhs_rank = preferred_column_rank(lhs);
        const int rhs_rank = preferred_column_rank(rhs);
        if (lhs_rank != rhs_rank)
            return lhs_rank < rhs_rank;
        return lhs < rhs;
    });
}

void clamp_selection(const KanbanBoard& board, KanbanSelection& selection)
{
    if (board.columns.empty())
    {
        selection = {};
        return;
    }

    selection.column = std::clamp(selection.column, 0, static_cast<int>(board.columns.size()) - 1);
    const auto& cards = board.columns[static_cast<size_t>(selection.column)].cards;
    if (cards.empty())
    {
        selection.card = 0;
        return;
    }
    selection.card = std::clamp(selection.card, 0, static_cast<int>(cards.size()) - 1);
}

bool selection_has_card(const KanbanBoard& board, KanbanSelection selection)
{
    if (selection.column < 0 || selection.column >= static_cast<int>(board.columns.size()))
        return false;
    const auto& cards = board.columns[static_cast<size_t>(selection.column)].cards;
    return selection.card >= 0 && selection.card < static_cast<int>(cards.size());
}

KanbanCard* selected_card(KanbanBoard& board, KanbanSelection selection)
{
    if (!selection_has_card(board, selection))
        return nullptr;
    return &board.columns[static_cast<size_t>(selection.column)].cards[static_cast<size_t>(selection.card)];
}

const KanbanCard* selected_card(const KanbanBoard& board, KanbanSelection selection)
{
    if (!selection_has_card(board, selection))
        return nullptr;
    return &board.columns[static_cast<size_t>(selection.column)].cards[static_cast<size_t>(selection.card)];
}

} // namespace draxul::kanban
```

- [ ] **Step 5: Add temporary CMake wiring for the library and test**

Add `add_subdirectory(libs/draxul-kanban)` in root `CMakeLists.txt` after `add_subdirectory(libs/draxul-host)`.

Add this initial `libs/draxul-kanban/CMakeLists.txt`:

```cmake
add_library(draxul-kanban STATIC
    src/kanban_board.cpp
)

target_include_directories(draxul-kanban PUBLIC
    include
)

target_link_libraries(draxul-kanban PUBLIC
    draxul-types
)
```

Add `draxul-kanban` to `target_link_libraries(draxul-tests PRIVATE ...)` in `tests/CMakeLists.txt`.

- [ ] **Step 6: Run the model tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][board]"
```

Expected: all `[kanban][board]` tests pass.

- [ ] **Step 7: Commit**

Run:

```powershell
git add CMakeLists.txt libs/draxul-kanban tests/CMakeLists.txt tests/kanban_board_tests.cpp
git commit -m "Add kanban board model"
```

---

### Task 2: Kanban Filesystem Store

**Files:**
- Create: `libs/draxul-kanban/include/draxul/kanban/kanban_store.h`
- Create: `libs/draxul-kanban/src/kanban_store.cpp`
- Modify: `libs/draxul-kanban/CMakeLists.txt`
- Create: `tests/kanban_store_tests.cpp`

- [ ] **Step 1: Write store tests**

Add `tests/kanban_store_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <draxul/kanban/kanban_store.h>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace draxul::kanban;

namespace
{

std::filesystem::path unique_test_root()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("draxul-kanban-" + std::to_string(stamp));
}

void write_file(const std::filesystem::path& path, std::string_view text = "# Card\n")
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("kanban store scans folders and markdown files", "[kanban][store]")
{
    const auto root = unique_test_root();
    write_file(root / "pending" / "alpha-feature.md");
    write_file(root / "done" / "fixed-bug.md");
    write_file(root / "pending" / "ignore.txt");

    std::string error;
    auto board = load_kanban_board(root, &error);
    REQUIRE(error.empty());
    REQUIRE(board.columns.size() == 2);
    REQUIRE(board.columns[0].name == "pending");
    REQUIRE(board.columns[0].cards.size() == 1);
    REQUIRE(board.columns[0].cards[0].file_name == "alpha-feature.md");
    REQUIRE(board.columns[0].cards[0].kind == CardKind::Feature);
    REQUIRE(board.columns[1].name == "done");

    std::filesystem::remove_all(root);
}

TEST_CASE("kanban store merges metadata order with newly discovered files", "[kanban][store]")
{
    const auto root = unique_test_root();
    write_file(root / "pending" / "a-feature.md");
    write_file(root / "pending" / "b-bug.md");
    write_file(root / "pending" / "c-refactor.md");
    write_file(root / "done" / "z.md");
    write_file(root / ".draxul-kanban.toml",
        "version = 1\n"
        "columns = [\"pending\", \"done\"]\n"
        "\n"
        "[cards]\n"
        "pending = [\"b-bug.md\", \"a-feature.md\"]\n"
        "done = [\"z.md\"]\n");

    std::string error;
    auto board = load_kanban_board(root, &error);
    REQUIRE(error.empty());
    REQUIRE(board.columns[0].cards[0].file_name == "b-bug.md");
    REQUIRE(board.columns[0].cards[1].file_name == "a-feature.md");
    REQUIRE(board.columns[0].cards[2].file_name == "c-refactor.md");

    std::filesystem::remove_all(root);
}

TEST_CASE("kanban reorder persists card order without moving files", "[kanban][store]")
{
    const auto root = unique_test_root();
    write_file(root / "pending" / "a-feature.md");
    write_file(root / "pending" / "b-bug.md");

    std::string error;
    auto board = load_kanban_board(root, &error);
    REQUIRE(error.empty());
    REQUIRE(reorder_card(board, KanbanSelection{ .column = 0, .card = 1 }, -1, &error));
    REQUIRE(save_kanban_order(board, &error));

    auto reloaded = load_kanban_board(root, &error);
    REQUIRE(error.empty());
    REQUIRE(reloaded.columns[0].cards[0].file_name == "b-bug.md");
    REQUIRE(std::filesystem::exists(root / "pending" / "b-bug.md"));

    std::filesystem::remove_all(root);
}

TEST_CASE("kanban move across columns renames the markdown file", "[kanban][store]")
{
    const auto root = unique_test_root();
    write_file(root / "pending" / "a-feature.md");
    std::filesystem::create_directories(root / "done");

    std::string error;
    auto board = load_kanban_board(root, &error);
    REQUIRE(error.empty());
    REQUIRE(move_card_to_column(board, KanbanSelection{ .column = 0, .card = 0 }, 1, &error));
    REQUIRE(save_kanban_order(board, &error));

    REQUIRE_FALSE(std::filesystem::exists(root / "pending" / "a-feature.md"));
    REQUIRE(std::filesystem::exists(root / "done" / "a-feature.md"));
    REQUIRE(board.columns[1].cards[0].path == root / "done" / "a-feature.md");

    std::filesystem::remove_all(root);
}
```

- [ ] **Step 2: Run the failing store tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][store]"
```

Expected: build fails because `kanban_store.h` does not exist.

- [ ] **Step 3: Add the store header**

Add `libs/draxul-kanban/include/draxul/kanban/kanban_store.h`:

```cpp
#pragma once

#include <draxul/kanban/kanban_board.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace draxul::kanban
{

inline constexpr std::string_view kKanbanMetadataFileName = ".draxul-kanban.toml";

std::filesystem::path resolve_kanban_root(
    const std::filesystem::path& source_path,
    const std::filesystem::path& working_dir,
    std::string* error);

KanbanBoard load_kanban_board(const std::filesystem::path& root, std::string* error);
bool save_kanban_order(const KanbanBoard& board, std::string* error);
bool reorder_card(KanbanBoard& board, KanbanSelection selection, int row_delta, std::string* error);
bool move_card_to_column(KanbanBoard& board, KanbanSelection selection, int target_column, std::string* error);

} // namespace draxul::kanban
```

- [ ] **Step 4: Implement scanning and metadata merge**

Add `libs/draxul-kanban/src/kanban_store.cpp`. Use this shape:

```cpp
#include <draxul/kanban/kanban_store.h>

#include <draxul/toml_support.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <unordered_map>

namespace draxul::kanban
{
namespace
{

struct Metadata
{
    std::vector<std::string> columns;
    std::unordered_map<std::string, std::vector<std::string>> cards_by_column;
};

bool is_hidden_name(const std::string& name)
{
    return !name.empty() && name[0] == '.';
}

bool is_markdown_file(const std::filesystem::path& path)
{
    return path.extension() == ".md";
}

std::optional<Metadata> load_metadata(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        return std::nullopt;

    std::string parse_error;
    auto document = toml_support::parse_file(path, &parse_error);
    if (!document)
        return std::nullopt;

    Metadata metadata;
    metadata.columns = toml_support::get_string_array(*document, "columns").value_or(std::vector<std::string>{});
    if (const toml::table* cards = (*document)["cards"].as_table())
    {
        for (const auto& [key, value] : *cards)
        {
            if (const toml::array* array = value.as_array())
            {
                std::vector<std::string> names;
                for (const toml::node& node : *array)
                {
                    if (const auto text = node.value<std::string>())
                        names.push_back(*text);
                }
                metadata.cards_by_column.emplace(std::string(key.str()), std::move(names));
            }
        }
    }
    return metadata;
}

std::vector<std::string> scan_column_names(const std::filesystem::path& root)
{
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_directory())
            continue;
        const std::string name = entry.path().filename().string();
        if (!is_hidden_name(name))
            names.push_back(name);
    }
    sort_columns_for_first_load(names);
    return names;
}

std::vector<KanbanCard> scan_cards(const std::filesystem::path& directory)
{
    std::vector<KanbanCard> cards;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || !is_markdown_file(entry.path()))
            continue;
        const std::string file_name = entry.path().filename().string();
        cards.push_back(KanbanCard{
            .file_name = file_name,
            .path = entry.path(),
            .kind = card_kind_for_file(file_name),
        });
    }
    std::ranges::sort(cards, {}, &KanbanCard::file_name);
    return cards;
}

void apply_card_order(std::vector<KanbanCard>& cards, const std::vector<std::string>& order)
{
    std::unordered_map<std::string, KanbanCard> by_name;
    for (auto& card : cards)
        by_name.emplace(card.file_name, std::move(card));

    std::vector<KanbanCard> ordered;
    ordered.reserve(cards.size());
    for (const std::string& name : order)
    {
        auto it = by_name.find(name);
        if (it == by_name.end())
            continue;
        ordered.push_back(std::move(it->second));
        by_name.erase(it);
    }

    std::vector<KanbanCard> remaining;
    for (auto& [_, card] : by_name)
        remaining.push_back(std::move(card));
    std::ranges::sort(remaining, {}, &KanbanCard::file_name);
    for (auto& card : remaining)
        ordered.push_back(std::move(card));
    cards = std::move(ordered);
}

} // namespace

std::filesystem::path resolve_kanban_root(
    const std::filesystem::path& source_path,
    const std::filesystem::path& working_dir,
    std::string* error)
{
    std::filesystem::path root = source_path;
    if (root.empty())
        root = working_dir.empty() ? std::filesystem::current_path() / "kanban" : working_dir / "kanban";
    else if (root.is_relative() && !working_dir.empty())
        root = working_dir / root;

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        if (error)
            *error = "Unable to create kanban root: " + ec.message();
        return {};
    }

    if (std::filesystem::is_empty(root, ec))
    {
        std::filesystem::create_directories(root / "ice-box", ec);
        std::filesystem::create_directories(root / "pending", ec);
        std::filesystem::create_directories(root / "done", ec);
    }

    const auto canonical = std::filesystem::weakly_canonical(root, ec);
    return ec ? root : canonical;
}

KanbanBoard load_kanban_board(const std::filesystem::path& root, std::string* error)
{
    if (error)
        error->clear();

    KanbanBoard board;
    board.root = root;

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
    {
        if (error)
            *error = "Kanban root is not a directory: " + root.string();
        return board;
    }

    auto names = scan_column_names(root);
    if (auto metadata = load_metadata(root / std::string(kKanbanMetadataFileName)))
    {
        std::vector<std::string> merged;
        std::set<std::string> seen;
        for (const std::string& name : metadata->columns)
        {
            if (std::ranges::find(names, name) != names.end() && seen.insert(name).second)
                merged.push_back(name);
        }
        for (const std::string& name : names)
        {
            if (seen.insert(name).second)
                merged.push_back(name);
        }
        names = std::move(merged);

        for (const std::string& name : names)
        {
            KanbanColumn column;
            column.name = name;
            column.directory = root / name;
            column.cards = scan_cards(column.directory);
            if (auto it = metadata->cards_by_column.find(name); it != metadata->cards_by_column.end())
                apply_card_order(column.cards, it->second);
            board.columns.push_back(std::move(column));
        }
        return board;
    }

    for (const std::string& name : names)
    {
        KanbanColumn column;
        column.name = name;
        column.directory = root / name;
        column.cards = scan_cards(column.directory);
        board.columns.push_back(std::move(column));
    }
    return board;
}

bool save_kanban_order(const KanbanBoard& board, std::string* error)
{
    if (error)
        error->clear();
    try
    {
        toml::table document;
        document.insert_or_assign("version", 1);

        toml::array columns;
        toml::table cards;
        for (const auto& column : board.columns)
        {
            columns.push_back(column.name);
            toml::array card_names;
            for (const auto& card : column.cards)
                card_names.push_back(card.file_name);
            cards.insert_or_assign(column.name, std::move(card_names));
        }
        document.insert_or_assign("columns", std::move(columns));
        document.insert_or_assign("cards", std::move(cards));

        std::filesystem::create_directories(board.root);
        std::ofstream out(board.root / std::string(kKanbanMetadataFileName), std::ios::trunc);
        if (!out)
        {
            if (error)
                *error = "Unable to open kanban metadata for writing.";
            return false;
        }
        out << document << '\n';
        return true;
    }
    catch (const std::exception& ex)
    {
        if (error)
            *error = ex.what();
        return false;
    }
}

bool reorder_card(KanbanBoard& board, KanbanSelection selection, int row_delta, std::string* error)
{
    if (error)
        error->clear();
    if (!selection_has_card(board, selection))
        return false;
    auto& cards = board.columns[static_cast<size_t>(selection.column)].cards;
    const int target = std::clamp(selection.card + row_delta, 0, static_cast<int>(cards.size()) - 1);
    if (target == selection.card)
        return true;
    std::iter_swap(cards.begin() + selection.card, cards.begin() + target);
    return true;
}

bool move_card_to_column(KanbanBoard& board, KanbanSelection selection, int target_column, std::string* error)
{
    if (error)
        error->clear();
    if (!selection_has_card(board, selection))
        return false;
    if (target_column < 0 || target_column >= static_cast<int>(board.columns.size()))
        return true;
    if (target_column == selection.column)
        return true;

    auto& source = board.columns[static_cast<size_t>(selection.column)];
    auto& target = board.columns[static_cast<size_t>(target_column)];
    KanbanCard card = source.cards[static_cast<size_t>(selection.card)];
    const std::filesystem::path destination = target.directory / card.file_name;
    if (std::filesystem::exists(destination))
    {
        if (error)
            *error = "Destination already contains " + card.file_name;
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(card.path, destination, ec);
    if (ec)
    {
        if (error)
            *error = "Failed to move " + card.file_name + ": " + ec.message();
        return false;
    }

    card.path = destination;
    source.cards.erase(source.cards.begin() + selection.card);
    const int insert_at = std::clamp(selection.card, 0, static_cast<int>(target.cards.size()));
    target.cards.insert(target.cards.begin() + insert_at, std::move(card));
    return true;
}

} // namespace draxul::kanban
```

- [ ] **Step 5: Update CMake**

Modify `libs/draxul-kanban/CMakeLists.txt`:

```cmake
add_library(draxul-kanban STATIC
    src/kanban_board.cpp
    src/kanban_store.cpp
)

target_include_directories(draxul-kanban PUBLIC
    include
)

target_link_libraries(draxul-kanban PUBLIC
    draxul-types
    draxul-config
)
```

- [ ] **Step 6: Run store tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][store]"
```

Expected: all `[kanban][store]` tests pass.

- [ ] **Step 7: Commit**

Run:

```powershell
git add libs/draxul-kanban tests/kanban_store_tests.cpp
git commit -m "Add kanban filesystem store"
```

---

### Task 3: Navigation Commands

**Files:**
- Create: `libs/draxul-kanban/include/draxul/kanban/kanban_navigation.h`
- Create: `libs/draxul-kanban/src/kanban_navigation.cpp`
- Modify: `libs/draxul-kanban/CMakeLists.txt`
- Create: `tests/kanban_navigation_tests.cpp`

- [ ] **Step 1: Write navigation tests**

Add `tests/kanban_navigation_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <draxul/kanban/kanban_navigation.h>

#include <SDL3/SDL.h>

using namespace draxul;
using namespace draxul::kanban;

namespace
{

KeyEvent key(int keycode, ModifierFlags mod = kModNone)
{
    return KeyEvent{ .scancode = 0, .keycode = keycode, .mod = mod, .pressed = true };
}

} // namespace

TEST_CASE("kanban navigation maps vim movement", "[kanban][navigation]")
{
    KanbanNavigationState nav;
    REQUIRE(nav.on_key(key(SDLK_J)) == KanbanNavigationCommand::SelectDown);
    REQUIRE(nav.on_key(key(SDLK_K)) == KanbanNavigationCommand::SelectUp);
    REQUIRE(nav.on_key(key(SDLK_H)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(nav.on_key(key(SDLK_L)) == KanbanNavigationCommand::SelectRight);
}

TEST_CASE("kanban navigation maps shifted movement to card moves", "[kanban][navigation]")
{
    KanbanNavigationState nav;
    REQUIRE(nav.on_key(key(SDLK_J, kModShift)) == KanbanNavigationCommand::MoveDown);
    REQUIRE(nav.on_key(key(SDLK_K, kModShift)) == KanbanNavigationCommand::MoveUp);
    REQUIRE(nav.on_key(key(SDLK_H, kModShift)) == KanbanNavigationCommand::MoveLeft);
    REQUIRE(nav.on_key(key(SDLK_L, kModShift)) == KanbanNavigationCommand::MoveRight);
}

TEST_CASE("kanban navigation maps arrows and enter", "[kanban][navigation]")
{
    KanbanNavigationState nav;
    REQUIRE(nav.on_key(key(SDLK_DOWN)) == KanbanNavigationCommand::SelectDown);
    REQUIRE(nav.on_key(key(SDLK_UP)) == KanbanNavigationCommand::SelectUp);
    REQUIRE(nav.on_key(key(SDLK_LEFT)) == KanbanNavigationCommand::SelectLeft);
    REQUIRE(nav.on_key(key(SDLK_RIGHT)) == KanbanNavigationCommand::SelectRight);
    REQUIRE(nav.on_key(key(SDLK_RETURN)) == KanbanNavigationCommand::Open);
}
```

- [ ] **Step 2: Run the failing navigation tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][navigation]"
```

Expected: build fails because the navigation header does not exist.

- [ ] **Step 3: Add the navigation header**

Add `libs/draxul-kanban/include/draxul/kanban/kanban_navigation.h`:

```cpp
#pragma once

#include <draxul/events.h>

namespace draxul::kanban
{

enum class KanbanNavigationCommand
{
    None,
    SelectLeft,
    SelectRight,
    SelectUp,
    SelectDown,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Open,
    Reload,
};

class KanbanNavigationState
{
public:
    KanbanNavigationCommand on_key(const draxul::KeyEvent& event);
};

} // namespace draxul::kanban
```

- [ ] **Step 4: Add the navigation implementation**

Add `libs/draxul-kanban/src/kanban_navigation.cpp`:

```cpp
#include <draxul/kanban/kanban_navigation.h>

#include <draxul/input_types.h>

#include <SDL3/SDL.h>

namespace draxul::kanban
{
namespace
{

ModifierFlags normalize_modifiers(ModifierFlags mod)
{
    ModifierFlags result = kModNone;
    if (mod & kModShift)
        result |= kModShift;
    if (mod & kModCtrl)
        result |= kModCtrl;
    if (mod & kModAlt)
        result |= kModAlt;
    if (mod & kModSuper)
        result |= kModSuper;
    return result;
}

bool has_only_modifiers(ModifierFlags actual, ModifierFlags expected)
{
    return normalize_modifiers(actual) == expected;
}

} // namespace

KanbanNavigationCommand KanbanNavigationState::on_key(const draxul::KeyEvent& event)
{
    if (!event.pressed)
        return KanbanNavigationCommand::None;

    if (has_only_modifiers(event.mod, kModShift))
    {
        switch (event.keycode)
        {
        case SDLK_H:
        case SDLK_LEFT:
            return KanbanNavigationCommand::MoveLeft;
        case SDLK_L:
        case SDLK_RIGHT:
            return KanbanNavigationCommand::MoveRight;
        case SDLK_K:
        case SDLK_UP:
            return KanbanNavigationCommand::MoveUp;
        case SDLK_J:
        case SDLK_DOWN:
            return KanbanNavigationCommand::MoveDown;
        default:
            return KanbanNavigationCommand::None;
        }
    }

    if (!has_only_modifiers(event.mod, kModNone))
        return KanbanNavigationCommand::None;

    switch (event.keycode)
    {
    case SDLK_H:
    case SDLK_LEFT:
        return KanbanNavigationCommand::SelectLeft;
    case SDLK_L:
    case SDLK_RIGHT:
        return KanbanNavigationCommand::SelectRight;
    case SDLK_K:
    case SDLK_UP:
        return KanbanNavigationCommand::SelectUp;
    case SDLK_J:
    case SDLK_DOWN:
        return KanbanNavigationCommand::SelectDown;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return KanbanNavigationCommand::Open;
    case SDLK_R:
        return KanbanNavigationCommand::Reload;
    default:
        return KanbanNavigationCommand::None;
    }
}

} // namespace draxul::kanban
```

- [ ] **Step 5: Update CMake and run tests**

Add `src/kanban_navigation.cpp` to `libs/draxul-kanban/CMakeLists.txt`.

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][navigation]"
```

Expected: all `[kanban][navigation]` tests pass.

- [ ] **Step 6: Commit**

Run:

```powershell
git add libs/draxul-kanban tests/kanban_navigation_tests.cpp
git commit -m "Add kanban vim navigation"
```

---

### Task 4: Grid Layout Helpers

**Files:**
- Create: `libs/draxul-kanban/include/draxul/kanban/kanban_layout.h`
- Create: `libs/draxul-kanban/src/kanban_layout.cpp`
- Modify: `libs/draxul-kanban/CMakeLists.txt`
- Create: `tests/kanban_layout_tests.cpp`

- [ ] **Step 1: Write layout tests**

Add `tests/kanban_layout_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <draxul/kanban/kanban_layout.h>

using namespace draxul::kanban;

TEST_CASE("kanban layout divides grid into equal columns", "[kanban][layout]")
{
    KanbanBoard board;
    board.columns.push_back(KanbanColumn{ .name = "ice-box" });
    board.columns.push_back(KanbanColumn{ .name = "pending" });
    board.columns.push_back(KanbanColumn{ .name = "done" });

    const auto layout = layout_kanban_board(board, KanbanSelection{}, KanbanLayoutOptions{
        .grid_cols = 90,
        .grid_rows = 20,
    });

    REQUIRE(layout.columns.size() == 3);
    REQUIRE(layout.columns[0].x == 0);
    REQUIRE(layout.columns[0].width == 30);
    REQUIRE(layout.columns[1].x == 30);
    REQUIRE(layout.columns[2].x == 60);
}

TEST_CASE("kanban layout marks selected card row", "[kanban][layout]")
{
    KanbanBoard board;
    board.columns.push_back(KanbanColumn{ .name = "pending" });
    board.columns[0].cards.push_back(KanbanCard{ .file_name = "a-feature.md", .kind = CardKind::Feature });
    board.columns[0].cards.push_back(KanbanCard{ .file_name = "b-bug.md", .kind = CardKind::Bug });

    const auto layout = layout_kanban_board(board, KanbanSelection{ .column = 0, .card = 1 },
        KanbanLayoutOptions{ .grid_cols = 40, .grid_rows = 10 });

    REQUIRE(layout.rows.size() == 2);
    REQUIRE(layout.rows[0].selected == false);
    REQUIRE(layout.rows[1].selected == true);
    REQUIRE(layout.rows[1].y == 3);
}

TEST_CASE("kanban text truncation respects cell budget", "[kanban][layout]")
{
    REQUIRE(truncate_to_cells("short.md", 20) == "short.md");
    REQUIRE(truncate_to_cells("very-long-card-name-feature.md", 12) == "very-long...");
    REQUIRE(truncate_to_cells("abc", 2) == "..");
}
```

- [ ] **Step 2: Run the failing layout tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][layout]"
```

Expected: build fails because `kanban_layout.h` does not exist.

- [ ] **Step 3: Add the layout header**

Add `libs/draxul-kanban/include/draxul/kanban/kanban_layout.h`:

```cpp
#pragma once

#include <draxul/kanban/kanban_board.h>

#include <string>
#include <vector>

namespace draxul::kanban
{

struct KanbanLayoutOptions
{
    int grid_cols = 1;
    int grid_rows = 1;
    int scroll_row = 0;
};

struct KanbanColumnLayout
{
    int x = 0;
    int width = 0;
    int index = 0;
};

struct KanbanCardRowLayout
{
    int column = 0;
    int card = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    bool selected = false;
};

struct KanbanLayout
{
    std::vector<KanbanColumnLayout> columns;
    std::vector<KanbanCardRowLayout> rows;
    int visible_card_rows = 0;
    int content_rows = 0;
};

KanbanLayout layout_kanban_board(
    const KanbanBoard& board,
    KanbanSelection selection,
    const KanbanLayoutOptions& options);

std::string truncate_to_cells(std::string_view text, int max_cells);
int next_scroll_row_for_selection(
    const KanbanBoard& board,
    KanbanSelection selection,
    int current_scroll_row,
    int grid_rows);

} // namespace draxul::kanban
```

- [ ] **Step 4: Add the layout implementation**

Add `libs/draxul-kanban/src/kanban_layout.cpp`:

```cpp
#include <draxul/kanban/kanban_layout.h>

#include <draxul/unicode.h>

#include <algorithm>
#include <chrono>

namespace draxul::kanban
{
namespace
{

constexpr int kHeaderRows = 3;
constexpr int kStatusRows = 1;
constexpr int kCardIndent = 1;

int visible_card_rows_for_grid(int grid_rows)
{
    return std::max(0, grid_rows - kHeaderRows - kStatusRows);
}

} // namespace

KanbanLayout layout_kanban_board(
    const KanbanBoard& board,
    KanbanSelection selection,
    const KanbanLayoutOptions& options)
{
    KanbanLayout layout;
    const int column_count = static_cast<int>(board.columns.size());
    if (column_count <= 0)
        return layout;

    const int base_width = std::max(1, options.grid_cols / column_count);
    int x = 0;
    for (int i = 0; i < column_count; ++i)
    {
        const int width = i == column_count - 1 ? std::max(1, options.grid_cols - x) : base_width;
        layout.columns.push_back(KanbanColumnLayout{ .x = x, .width = width, .index = i });
        x += width;
    }

    layout.visible_card_rows = visible_card_rows_for_grid(options.grid_rows);
    int max_cards = 0;
    for (const auto& column : board.columns)
        max_cards = std::max(max_cards, static_cast<int>(column.cards.size()));
    layout.content_rows = max_cards;

    for (int col = 0; col < column_count; ++col)
    {
        const auto& cards = board.columns[static_cast<size_t>(col)].cards;
        const auto& column_layout = layout.columns[static_cast<size_t>(col)];
        for (int row = 0; row < static_cast<int>(cards.size()); ++row)
        {
            const int visible_row = row - options.scroll_row;
            if (visible_row < 0 || visible_row >= layout.visible_card_rows)
                continue;
            layout.rows.push_back(KanbanCardRowLayout{
                .column = col,
                .card = row,
                .x = column_layout.x + kCardIndent,
                .y = kHeaderRows + visible_row,
                .width = std::max(1, column_layout.width - 2),
                .selected = selection.column == col && selection.card == row,
            });
        }
    }

    return layout;
}

std::string truncate_to_cells(std::string_view text, int max_cells)
{
    if (max_cells <= 0)
        return {};
    if (max_cells <= 3)
        return std::string(static_cast<size_t>(max_cells), '.');

    std::string out;
    int used = 0;
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t cluster_start = offset;
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;
        const std::string_view cluster = text.substr(cluster_start, offset - cluster_start);
        const int width = std::max(1, cluster_cell_width(cluster));
        if (used + width > max_cells)
            break;
        out.append(cluster);
        used += width;
    }

    if (offset == text.size())
        return out;

    while (!out.empty() && used + 3 > max_cells)
    {
        out.pop_back();
        used = 0;
        size_t recalc_offset = 0;
        while (recalc_offset < out.size())
        {
            const size_t cluster_start = recalc_offset;
            uint32_t cp = 0;
            if (!utf8_decode_next(out, recalc_offset, cp))
                break;
            used += std::max(1, cluster_cell_width(std::string_view(out).substr(cluster_start, recalc_offset - cluster_start)));
        }
    }
    out += "...";
    return out;
}

int next_scroll_row_for_selection(
    const KanbanBoard&,
    KanbanSelection selection,
    int current_scroll_row,
    int grid_rows)
{
    const int visible_rows = visible_card_rows_for_grid(grid_rows);
    if (visible_rows <= 0)
        return 0;
    if (selection.card < current_scroll_row)
        return selection.card;
    if (selection.card >= current_scroll_row + visible_rows)
        return selection.card - visible_rows + 1;
    return std::max(0, current_scroll_row);
}

} // namespace draxul::kanban
```

- [ ] **Step 5: Update CMake and run tests**

Add `src/kanban_layout.cpp` to `libs/draxul-kanban/CMakeLists.txt`.

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban][layout]"
```

Expected: all `[kanban][layout]` tests pass.

- [ ] **Step 6: Commit**

Run:

```powershell
git add libs/draxul-kanban tests/kanban_layout_tests.cpp
git commit -m "Add kanban grid layout"
```

---

### Task 5: Markdown Host Reuse Callback

**Files:**
- Modify: `libs/draxul-host/include/draxul/host.h`
- Modify: `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`
- Modify: `libs/draxul-markdown/src/markdown_host.cpp`
- Modify: `app/app.h`
- Modify: `app/app.cpp`

- [ ] **Step 1: Add host callback API**

In `libs/draxul-host/include/draxul/host.h`, add this method to `IHostCallbacks` after `dispatch_to_nvim_host()`:

```cpp
    // Open a Markdown source in a Markdown host. App implementations should
    // reuse an existing Markdown pane when one exists, otherwise create a
    // vertical split with HostKind::Markdown and the supplied source path.
    virtual bool open_markdown_source(std::string_view /*path*/)
    {
        return false;
    }
```

In the same file, add this method to `IHost` after `is_nvim_host()`:

```cpp
    virtual bool is_markdown_host() const
    {
        return false;
    }
```

- [ ] **Step 2: Make MarkdownHost identifiable and reopenable**

In `libs/draxul-markdown/include/draxul/markdown/markdown_host.h`, add:

```cpp
    bool is_markdown_host() const override;
```

In `libs/draxul-markdown/src/markdown_host.cpp`, update `dispatch_action()`:

```cpp
bool MarkdownHost::dispatch_action(std::string_view action)
{
    constexpr std::string_view open_prefix = "open_file:";
    if (action.starts_with(open_prefix))
    {
        HostLaunchOptions launch;
        launch.kind = HostKind::Markdown;
        launch.source_path = std::string(action.substr(open_prefix.size()));
        return load_source(launch);
    }
    if (action == "reload")
        return load_source(HostLaunchOptions{ .kind = HostKind::Markdown, .source_path = source_path_.string() });
    if (action == "font_increase")
        return change_font_size(base_point_size_ + 0.5f);
    if (action == "font_decrease")
        return change_font_size(base_point_size_ - 0.5f);
    if (action == "font_reset")
        return change_font_size(TextService::DEFAULT_POINT_SIZE);
    return false;
}
```

Add the method implementation:

```cpp
bool MarkdownHost::is_markdown_host() const
{
    return true;
}
```

- [ ] **Step 3: Add App callback declaration**

In `app/app.h`, add next to `dispatch_to_nvim_host()`:

```cpp
    bool open_markdown_source(std::string_view path) override;
```

- [ ] **Step 4: Implement App Markdown open/reuse**

In `app/app.cpp`, add after `App::dispatch_to_nvim_host()`:

```cpp
bool App::open_markdown_source(std::string_view path)
{
    IHost* markdown_host = nullptr;
    LeafId markdown_leaf = kInvalidLeaf;
    active_host_manager().for_each_host([&markdown_host, &markdown_leaf](LeafId id, IHost& host) {
        if (!markdown_host && host.is_markdown_host())
        {
            markdown_host = &host;
            markdown_leaf = id;
        }
    });

    const std::string action = "open_file:" + std::string(path);
    if (markdown_host)
    {
        if (!markdown_host->dispatch_action(action))
        {
            push_toast(2, "Failed to open markdown card.");
            return false;
        }
        active_host_manager().set_focused(markdown_leaf);
        request_frame();
        return true;
    }

    HostLaunchOptions launch;
    launch.kind = HostKind::Markdown;
    launch.source_path = std::string(path);
    LeafId new_leaf = active_host_manager().split_focused(SplitDirection::Vertical, std::move(launch), *this);
    if (new_leaf == kInvalidLeaf)
    {
        const std::string& err = active_host_manager().error();
        push_toast(2, err.empty() ? std::string("Failed to open markdown split") : err);
        return false;
    }

    refresh_window_layout();
    request_frame();
    return true;
}
```

- [ ] **Step 5: Build and run Markdown tests**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[markdown]"
```

Expected: Markdown tests pass.

- [ ] **Step 6: Commit**

Run:

```powershell
git add libs/draxul-host/include/draxul/host.h libs/draxul-markdown app/app.h app/app.cpp
git commit -m "Allow hosts to open markdown sources"
```

---

### Task 6: Kanban Grid Host

**Files:**
- Create: `libs/draxul-kanban/include/draxul/kanban/kanban_host.h`
- Create: `libs/draxul-kanban/src/kanban_host.cpp`
- Modify: `libs/draxul-kanban/CMakeLists.txt`

- [ ] **Step 1: Add host header**

Add `libs/draxul-kanban/include/draxul/kanban/kanban_host.h`:

```cpp
#pragma once

#include <draxul/grid_host_base.h>
#include <draxul/kanban/kanban_board.h>
#include <draxul/kanban/kanban_navigation.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace draxul
{
class HostProviderRegistry;
}

namespace draxul::kanban
{

class KanbanHost final : public draxul::GridHostBase
{
public:
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    void pump() override;
    void on_focus_gained() override;
    void on_key(const draxul::KeyEvent& event) override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;

private:
    bool initialize_host() override;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override;
    std::string_view host_name() const override;

    void configure_highlights();
    void reload_board();
    void redraw_board();
    void apply_navigation_command(KanbanNavigationCommand command);
    void move_selection(int column_delta, int card_delta);
    void move_card(int column_delta, int row_delta);
    void open_selected_card();
    void keep_selection_visible();
    void write_text(int col, int row, std::string_view text, uint16_t hl, int max_cells);
    void fill_row(int row, int col, int width, uint16_t hl);

    std::filesystem::path root_;
    KanbanBoard board_;
    KanbanSelection selection_;
    KanbanNavigationState navigation_;
    std::string init_error_;
    std::string status_;
    bool running_ = false;
    bool redraw_needed_ = true;
    int scroll_row_ = 0;
};

std::unique_ptr<draxul::IHost> create_kanban_host();
void register_kanban_host_provider(draxul::HostProviderRegistry& registry);

} // namespace draxul::kanban
```

- [ ] **Step 2: Add host implementation**

Add `libs/draxul-kanban/src/kanban_host.cpp` using these constants and method bodies:

```cpp
#include <draxul/kanban/kanban_host.h>

#include <draxul/host_registry.h>
#include <draxul/kanban/kanban_layout.h>
#include <draxul/kanban/kanban_store.h>
#include <draxul/unicode.h>

#include <algorithm>

namespace draxul::kanban
{
namespace
{

enum HighlightId : uint16_t
{
    HlNormal = 1,
    HlHeader = 2,
    HlHeaderActive = 3,
    HlCard = 4,
    HlSelected = 5,
    HlBorder = 6,
    HlBug = 7,
    HlFeature = 8,
    HlRefactor = 9,
    HlStatus = 10,
};

uint16_t icon_highlight(CardKind kind)
{
    switch (kind)
    {
    case CardKind::Bug:
        return HlBug;
    case CardKind::Feature:
        return HlFeature;
    case CardKind::Refactor:
        return HlRefactor;
    case CardKind::Note:
        return HlCard;
    }
    return HlCard;
}

} // namespace

bool KanbanHost::initialize_host()
{
    configure_highlights();
    std::string error;
    root_ = resolve_kanban_root(launch_options().source_path, launch_options().working_dir, &error);
    if (!error.empty())
    {
        init_error_ = error;
        return false;
    }
    reload_board();
    running_ = init_error_.empty();
    set_content_ready(running_);
    callbacks().set_window_title("Kanban");
    return running_;
}

void KanbanHost::shutdown()
{
    running_ = false;
}

bool KanbanHost::is_running() const
{
    return running_;
}

std::string KanbanHost::init_error() const
{
    return init_error_;
}

void KanbanHost::on_focus_gained()
{
    GridHostBase::on_focus_gained();
    suppress_cursor_until(std::chrono::steady_clock::now() + std::chrono::hours(24));
}

void KanbanHost::pump()
{
    if (!running_)
        return;
    suppress_cursor_until(std::chrono::steady_clock::now() + std::chrono::hours(24));
    if (redraw_needed_)
        redraw_board();
    advance_cursor_blink(std::chrono::steady_clock::now());
}

void KanbanHost::on_key(const draxul::KeyEvent& event)
{
    const KanbanNavigationCommand command = navigation_.on_key(event);
    if (command == KanbanNavigationCommand::None)
        return;
    apply_navigation_command(command);
}

bool KanbanHost::dispatch_action(std::string_view action)
{
    if (action == "reload")
    {
        reload_board();
        return true;
    }
    return false;
}

void KanbanHost::request_close()
{
    running_ = false;
}

std::string KanbanHost::status_text() const
{
    return status_;
}

void KanbanHost::on_viewport_changed()
{
    apply_grid_size(std::max(1, viewport().grid_size.x), std::max(1, viewport().grid_size.y));
    redraw_needed_ = true;
}

void KanbanHost::on_font_metrics_changed_impl()
{
    redraw_needed_ = true;
}

std::string_view KanbanHost::host_name() const
{
    return "Kanban";
}

void KanbanHost::configure_highlights()
{
    highlights().set_default_fg(Color(0.88f, 0.90f, 0.92f, 1.0f));
    highlights().set_default_bg(Color(0.055f, 0.060f, 0.070f, 1.0f));

    highlights().set(HlNormal, HlAttr{ .fg = Color(0.88f, 0.90f, 0.92f, 1.0f), .has_fg = true });
    highlights().set(HlHeader, HlAttr{
        .fg = Color(0.96f, 0.92f, 0.78f, 1.0f),
        .bg = Color(0.12f, 0.13f, 0.15f, 1.0f),
        .has_fg = true,
        .has_bg = true,
        .bold = true,
    });
    highlights().set(HlHeaderActive, HlAttr{
        .fg = Color(0.98f, 0.96f, 0.88f, 1.0f),
        .bg = Color(0.18f, 0.20f, 0.24f, 1.0f),
        .has_fg = true,
        .has_bg = true,
        .bold = true,
    });
    highlights().set(HlCard, HlAttr{ .fg = Color(0.84f, 0.87f, 0.91f, 1.0f), .has_fg = true });
    highlights().set(HlSelected, HlAttr{
        .fg = Color(0.99f, 0.99f, 0.99f, 1.0f),
        .bg = Color(0.20f, 0.28f, 0.38f, 1.0f),
        .has_fg = true,
        .has_bg = true,
        .bold = true,
    });
    highlights().set(HlBorder, HlAttr{ .fg = Color(0.32f, 0.35f, 0.40f, 1.0f), .has_fg = true });
    highlights().set(HlBug, HlAttr{ .fg = Color(0.95f, 0.44f, 0.44f, 1.0f), .has_fg = true });
    highlights().set(HlFeature, HlAttr{ .fg = Color(0.44f, 0.74f, 0.95f, 1.0f), .has_fg = true });
    highlights().set(HlRefactor, HlAttr{ .fg = Color(0.70f, 0.62f, 0.93f, 1.0f), .has_fg = true });
    highlights().set(HlStatus, HlAttr{ .fg = Color(0.62f, 0.66f, 0.72f, 1.0f), .has_fg = true });
}

void KanbanHost::reload_board()
{
    std::string error;
    board_ = load_kanban_board(root_, &error);
    if (!error.empty())
    {
        init_error_ = error;
        callbacks().push_toast(2, error);
    }
    clamp_selection(board_, selection_);
    keep_selection_visible();
    redraw_needed_ = true;
}

void KanbanHost::redraw_board()
{
    grid().clear();
    const auto layout = layout_kanban_board(board_, selection_, KanbanLayoutOptions{
        .grid_cols = grid_cols(),
        .grid_rows = grid_rows(),
        .scroll_row = scroll_row_,
    });

    for (const auto& column_layout : layout.columns)
    {
        const auto& column = board_.columns[static_cast<size_t>(column_layout.index)];
        const bool active_column = selection_.column == column_layout.index;
        fill_row(0, column_layout.x, column_layout.width, active_column ? HlHeaderActive : HlHeader);
        write_text(column_layout.x + 1, 0, column.name, active_column ? HlHeaderActive : HlHeader, column_layout.width - 2);
        for (int y = 1; y < std::max(1, grid_rows() - 1); ++y)
            grid().set_cell(column_layout.x, y, "|", HlBorder, false);
    }

    for (const auto& row : layout.rows)
    {
        const auto& card = board_.columns[static_cast<size_t>(row.column)].cards[static_cast<size_t>(row.card)];
        const uint16_t text_hl = row.selected ? HlSelected : HlCard;
        if (row.selected)
            fill_row(row.y, row.x - 1, row.width + 1, HlSelected);
        std::string icon = icon_for_kind(card.kind);
        write_text(row.x, row.y, icon, icon_highlight(card.kind), 2);
        write_text(row.x + 3, row.y, truncate_to_cells(card.file_name, row.width - 3), text_hl, row.width - 3);
    }

    status_ = "kanban | " + root_.string();
    if (const auto* card = selected_card(board_, selection_))
        status_ += " | " + card->file_name;
    if (grid_rows() > 0)
        write_text(0, grid_rows() - 1, status_, HlStatus, grid_cols());

    set_cursor_display_override(std::pair<int, int>{ 0, 0 });
    flush_grid();
    redraw_needed_ = false;
}

void KanbanHost::apply_navigation_command(KanbanNavigationCommand command)
{
    switch (command)
    {
    case KanbanNavigationCommand::SelectLeft:
        move_selection(-1, 0);
        break;
    case KanbanNavigationCommand::SelectRight:
        move_selection(1, 0);
        break;
    case KanbanNavigationCommand::SelectUp:
        move_selection(0, -1);
        break;
    case KanbanNavigationCommand::SelectDown:
        move_selection(0, 1);
        break;
    case KanbanNavigationCommand::MoveLeft:
        move_card(-1, 0);
        break;
    case KanbanNavigationCommand::MoveRight:
        move_card(1, 0);
        break;
    case KanbanNavigationCommand::MoveUp:
        move_card(0, -1);
        break;
    case KanbanNavigationCommand::MoveDown:
        move_card(0, 1);
        break;
    case KanbanNavigationCommand::Open:
        open_selected_card();
        break;
    case KanbanNavigationCommand::Reload:
        reload_board();
        break;
    case KanbanNavigationCommand::None:
        break;
    }
}

void KanbanHost::move_selection(int column_delta, int card_delta)
{
    selection_.column += column_delta;
    selection_.card += card_delta;
    clamp_selection(board_, selection_);
    keep_selection_visible();
    redraw_needed_ = true;
    callbacks().request_frame();
}

void KanbanHost::move_card(int column_delta, int row_delta)
{
    if (!selection_has_card(board_, selection_))
        return;

    std::string error;
    bool changed = false;
    if (column_delta != 0)
    {
        const int target_column = selection_.column + column_delta;
        changed = move_card_to_column(board_, selection_, target_column, &error);
        if (changed && error.empty())
            selection_.column = std::clamp(target_column, 0, static_cast<int>(board_.columns.size()) - 1);
    }
    else if (row_delta != 0)
    {
        changed = reorder_card(board_, selection_, row_delta, &error);
        if (changed && error.empty())
            selection_.card += row_delta;
    }

    if (!error.empty())
    {
        callbacks().push_toast(2, error);
        return;
    }
    clamp_selection(board_, selection_);
    if (changed && !save_kanban_order(board_, &error))
        callbacks().push_toast(2, error.empty() ? "Failed to save kanban order." : error);
    keep_selection_visible();
    redraw_needed_ = true;
    callbacks().request_frame();
}

void KanbanHost::open_selected_card()
{
    const KanbanCard* card = selected_card(board_, selection_);
    if (card == nullptr)
        return;
    if (!callbacks().open_markdown_source(card->path.string()))
        callbacks().push_toast(2, "Failed to open markdown card.");
}

void KanbanHost::keep_selection_visible()
{
    scroll_row_ = next_scroll_row_for_selection(board_, selection_, scroll_row_, grid_rows());
}

void KanbanHost::write_text(int col, int row, std::string_view text, uint16_t hl, int max_cells)
{
    if (row < 0 || row >= grid_rows() || max_cells <= 0)
        return;

    int used = 0;
    size_t offset = 0;
    while (offset < text.size() && used < max_cells)
    {
        const size_t cluster_start = offset;
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;
        const std::string cluster(text.substr(cluster_start, offset - cluster_start));
        int width = std::max(1, cluster_cell_width(cluster));
        if (used + width > max_cells)
            break;
        grid().set_cell(col + used, row, cluster, hl, width == 2);
        used += width;
    }
}

void KanbanHost::fill_row(int row, int col, int width, uint16_t hl)
{
    for (int x = std::max(0, col); x < std::min(grid_cols(), col + width); ++x)
        grid().set_cell(x, row, " ", hl, false);
}

std::unique_ptr<draxul::IHost> create_kanban_host()
{
    return std::make_unique<KanbanHost>();
}

void register_kanban_host_provider(draxul::HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::Kanban, &create_kanban_host);
}

} // namespace draxul::kanban
```

- [ ] **Step 3: Fix row movement selection clamping**

In `move_card()`, after successful `reorder_card()`, clamp the row update to the current column card count:

```cpp
selection_.card = std::clamp(
    selection_.card + row_delta,
    0,
    std::max(0, static_cast<int>(board_.columns[static_cast<size_t>(selection_.column)].cards.size()) - 1));
```

Use this in place of the plain `selection_.card += row_delta;` line from Step 2.

- [ ] **Step 4: Update CMake**

Modify `libs/draxul-kanban/CMakeLists.txt` to include `src/kanban_host.cpp` and link `draxul-host`:

```cmake
add_library(draxul-kanban STATIC
    src/kanban_board.cpp
    src/kanban_store.cpp
    src/kanban_navigation.cpp
    src/kanban_layout.cpp
    src/kanban_host.cpp
)

target_include_directories(draxul-kanban PUBLIC
    include
)

target_link_libraries(draxul-kanban PUBLIC
    draxul-types
    draxul-config
    draxul-host
)
```

- [ ] **Step 5: Build**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
```

Expected: build passes. If there are compile errors in the host, fix them inside `kanban_host.cpp` and keep public interfaces unchanged unless the error proves the interface is invalid.

- [ ] **Step 6: Commit**

Run:

```powershell
git add libs/draxul-kanban
git commit -m "Add kanban grid host"
```

---

### Task 7: App Wiring And Host Kind

**Files:**
- Modify: `libs/draxul-types/include/draxul/host_kind.h`
- Modify: `app/main.cpp`
- Modify: `app/command_palette.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add host kind**

In `libs/draxul-types/include/draxul/host_kind.h`:

```cpp
enum class HostKind
{
    Nvim,
    PowerShell,
    Bash,
    Zsh,
    Wsl,
    MegaCity,
    NanoVGDemo,
    Markdown,
    Kanban,
};
```

Add parsing:

```cpp
    if (normalized == "kanban" || normalized == "kb")
        return HostKind::Kanban;
```

Add string conversion:

```cpp
    case HostKind::Kanban:
        return "kanban";
```

Update `is_terminal_shell_host()` in `app/host_manager.cpp` so `Kanban` returns false in the non-shell branch.

- [ ] **Step 2: Register provider**

In `app/main.cpp`, include:

```cpp
#include <draxul/kanban/kanban_host.h>
```

Register after Markdown:

```cpp
    draxul::kanban::register_kanban_host_provider(host_registry);
```

- [ ] **Step 3: Link the executable and tests**

In root `CMakeLists.txt`:

- Add `draxul-kanban` to the sanitizer/coverage library list.
- Link `draxul` with `draxul-kanban`:

```cmake
target_link_libraries(draxul PRIVATE draxul-app draxul-markdown-host draxul-kanban)
```

In `tests/CMakeLists.txt`, keep `draxul-kanban` linked to `draxul-tests`.

- [ ] **Step 4: Add command palette entries**

In `app/command_palette.cpp`, add `HostKind::Kanban` to the `host_kinds` vector:

```cpp
    std::vector<HostKind> host_kinds = {
        HostKind::Nvim,
        HostKind::Kanban,
        HostKind::MegaCity,
```

- [ ] **Step 5: Build the app**

Run:

```powershell
cmake --build build --config Release --target draxul draxul-tests
```

Expected: build passes.

- [ ] **Step 6: Commit**

Run:

```powershell
git add CMakeLists.txt app/main.cpp app/command_palette.cpp app/host_manager.cpp libs/draxul-types/include/draxul/host_kind.h tests/CMakeLists.txt
git commit -m "Register kanban host"
```

---

### Task 8: Documentation And Manual Smoke

**Files:**
- Modify: `docs/features.md`

- [ ] **Step 1: Update features documentation**

In `docs/features.md`, add a Host Types row:

```markdown
| Kanban | `--host kanban [--source <kanban-folder>]` | Native grid-backed Kanban viewer for local Markdown work items. Child folders under the board root become columns, Markdown files become cards, `.draxul-kanban.toml` preserves column/card ordering, Vim-style `j/k/h/l` moves the selection, `Shift+j/k/h/l` reorders or moves cards between folders, and Enter opens the selected card in a Markdown viewer split or reuses an existing Markdown pane. |
```

Add an Input bullet:

```markdown
- **Kanban navigation**: In `--host kanban`, `j/k/h/l` and arrows move the active card selection, `Shift+j/k` reorders within a column, `Shift+h/l` moves the Markdown file between adjacent column folders, `r` reloads the board from disk, and Enter opens the selected card in the Markdown viewer.
```

- [ ] **Step 2: Run focused tests**

Run:

```powershell
cmake --build build --config Release --target draxul draxul-tests
.\build\tests\Release\draxul-tests.exe "[kanban]"
.\build\tests\Release\draxul-tests.exe "[markdown]"
```

Expected: all focused tests pass.

- [ ] **Step 3: Run full validation**

Run:

```powershell
ctest --test-dir build --build-config Release --output-on-failure
python do.py smoke
git diff --check
```

Expected:
- `ctest` passes.
- smoke test passes.
- `git diff --check` reports no errors. Line-ending warnings can be noted if they match existing repository behavior and no whitespace errors are reported.

- [ ] **Step 4: Manual launch check**

Run:

```powershell
.\build\Release\draxul.exe --console --host kanban --source kanban --log-file kanban-host.log --log-level debug
```

Manual checks:
- The board shows one column per folder under `kanban/`.
- The active card row has a visible background highlight.
- `j/k/h/l` changes the active card.
- `Shift+j/k` changes order and updates `kanban/.draxul-kanban.toml`.
- `Shift+h/l` moves the selected `.md` file between folder directories.
- Enter opens the selected Markdown file in a vertical split or reuses an existing Markdown pane.

- [ ] **Step 5: Commit**

Run:

```powershell
git add docs/features.md
git commit -m "Document kanban host"
```

---

## Final Review Checklist

- [ ] `--host kanban` is parseable and appears in command palette split/new-tab host actions.
- [ ] `--source <folder>` selects the board root.
- [ ] Empty/default board root becomes usable with three folders: `ice-box`, `pending`, `done`.
- [ ] Existing `kanban/` folders become columns without hardcoded names.
- [ ] `.draxul-kanban.toml` is written inside the board root and contains column order plus per-column card order.
- [ ] Up/down card moves do not move files on disk.
- [ ] Left/right card moves rename the file into the adjacent column folder.
- [ ] Destination filename collisions leave the source file unchanged and show a toast.
- [ ] Unicode icon clusters render as one grid item and do not corrupt following text.
- [ ] Long filenames truncate with `...` and do not overflow into the next column.
- [ ] Enter reuses an existing Markdown host before creating a split.
- [ ] Focus moves to the Markdown host after Enter.
- [ ] Focused Kanban pane still reports useful status text.
- [ ] `docs/features.md` documents host flag, metadata file, and keybindings.
- [ ] Focused Kanban tests, focused Markdown tests, full `ctest`, `python do.py smoke`, and `git diff --check` have been run.

## Suggested Subagent Execution

1. Dispatch Agent A for Tasks 1 and 2.
2. Dispatch Agent B for Tasks 3 and 4 in parallel with Agent A.
3. Dispatch Agent C for Task 5 in parallel with Agents A and B.
4. Main integrator reviews those branches/patches, then implements Tasks 6 and 7.
5. Dispatch a reviewer agent for the completed tree while main integrator runs Task 8 validation.
