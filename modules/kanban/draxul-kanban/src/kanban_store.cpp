#include <draxul/kanban/kanban_store.h>

#include "kanban_directory_scan.h"

#include <draxul/string_util.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace draxul::kanban
{
namespace
{
thread_local KanbanDirectoryOperations* directory_operations_override
    = nullptr;

class NativeKanbanDirectoryCursor final
    : public KanbanDirectoryCursor
{
public:
    NativeKanbanDirectoryCursor(const std::filesystem::path& directory,
        std::error_code& error)
        : iterator_(directory, error)
    {
    }

    bool at_end() const noexcept override
    {
        return iterator_ == end_;
    }

    const std::filesystem::directory_entry& entry() const override
    {
        return *iterator_;
    }

    bool increment(std::error_code& error) override
    {
        iterator_.increment(error);
        return !error;
    }

private:
    std::filesystem::directory_iterator iterator_;
    std::filesystem::directory_iterator end_;
};

class NativeKanbanDirectoryOperations final
    : public KanbanDirectoryOperations
{
public:
    std::unique_ptr<KanbanDirectoryCursor> open(
        const std::filesystem::path& directory,
        std::error_code& error) override
    {
        auto cursor = std::make_unique<NativeKanbanDirectoryCursor>(
            directory, error);
        if (error)
            return {};
        return cursor;
    }
};

KanbanDirectoryOperations& active_directory_operations()
{
    if (directory_operations_override)
        return *directory_operations_override;
    return *native_kanban_directory_operations();
}

using OrderedNames = std::vector<std::string>;

struct KanbanMetadata
{
    OrderedNames columns;
    std::unordered_map<std::string, OrderedNames> cards_by_column;
};

void set_error(std::string* error, std::string message)
{
    if (error)
    {
        *error = std::move(message);
    }
}

void clear_error(std::string* error)
{
    if (error)
    {
        error->clear();
    }
}

OrderedNames parse_string_array(std::string_view value)
{
    OrderedNames names;
    const auto open = value.find('[');
    const auto close = value.rfind(']');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
    {
        return names;
    }

    bool in_string = false;
    bool escaped = false;
    std::string current;
    for (char ch : value.substr(open + 1, close - open - 1))
    {
        if (in_string && escaped)
        {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (in_string && ch == '\\')
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            if (in_string)
            {
                names.push_back(current);
                current.clear();
            }
            in_string = !in_string;
            continue;
        }
        if (in_string)
        {
            current.push_back(ch);
        }
    }
    return names;
}

std::optional<KanbanMetadata> read_metadata(const std::filesystem::path& root, std::string* error)
{
    const auto path = root / std::string(kKanbanMetadataFileName);
    if (!std::filesystem::exists(path))
    {
        return std::nullopt;
    }

    std::ifstream file(path);
    if (!file)
    {
        set_error(error, "failed to open kanban metadata: " + path.string());
        return std::nullopt;
    }

    KanbanMetadata metadata;
    bool in_cards = false;
    std::string line;
    while (std::getline(file, line))
    {
        const auto stripped = trim(line);
        if (stripped.empty() || stripped.starts_with('#'))
        {
            continue;
        }
        if (stripped == "[cards]")
        {
            in_cards = true;
            continue;
        }

        const auto equals = stripped.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }
        const auto key = trim(std::string_view(stripped).substr(0, equals));
        const auto value = std::string_view(stripped).substr(equals + 1);
        if (in_cards)
        {
            metadata.cards_by_column[key] = parse_string_array(value);
        }
        else if (key == "columns")
        {
            metadata.columns = parse_string_array(value);
        }
    }

    return metadata;
}

bool contains_name(const OrderedNames& names, const std::string& name)
{
    return std::ranges::find(names, name) != names.end();
}

void apply_column_metadata(std::vector<KanbanColumn>& columns, const OrderedNames& metadata_order)
{
    std::vector<KanbanColumn> ordered;
    ordered.reserve(columns.size());
    std::vector<bool> used(columns.size(), false);

    for (const auto& name : metadata_order)
    {
        const auto it = std::ranges::find(columns, name, &KanbanColumn::name);
        if (it != columns.end())
        {
            used[static_cast<size_t>(std::distance(columns.begin(), it))] = true;
            ordered.push_back(std::move(*it));
        }
    }

    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (!used[i])
        {
            ordered.push_back(std::move(columns[i]));
        }
    }

    columns = std::move(ordered);
}

void apply_card_metadata(KanbanColumn& column, const OrderedNames& metadata_order)
{
    std::vector<KanbanCard> ordered;
    ordered.reserve(column.cards.size());
    std::vector<bool> used(column.cards.size(), false);

    for (const auto& name : metadata_order)
    {
        const auto it = std::ranges::find(column.cards, name, &KanbanCard::file_name);
        if (it != column.cards.end())
        {
            used[static_cast<size_t>(std::distance(column.cards.begin(), it))] = true;
            ordered.push_back(std::move(*it));
        }
    }

    for (size_t i = 0; i < column.cards.size(); ++i)
    {
        if (!used[i])
        {
            ordered.push_back(std::move(column.cards[i]));
        }
    }

    column.cards = std::move(ordered);
}

std::string quote(std::string_view value)
{
    std::string out = "\"";
    for (char ch : value)
    {
        if (ch == '"' || ch == '\\')
        {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

void write_string_array(std::ostream& out, const OrderedNames& names)
{
    out << "[";
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
        {
            out << ", ";
        }
        out << quote(names[i]);
    }
    out << "]";
}

std::filesystem::path normalized_path(const std::filesystem::path& path)
{
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec)
        normalized = std::filesystem::absolute(path, ec);
    return ec ? path.lexically_normal() : normalized;
}

bool path_is_within(
    const std::filesystem::path& parent,
    const std::filesystem::path& child)
{
    const auto relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute())
        return false;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

std::string unquote_path(std::string value)
{
    value = trim(value);
    if (value.size() >= 2
        && ((value.front() == '"' && value.back() == '"')
            || (value.front() == '\'' && value.back() == '\'')))
    {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

void discover_submodule_boards(
    const std::filesystem::path& repository_root,
    const std::filesystem::path& workspace_root,
    std::set<std::filesystem::path>& visited_repositories,
    std::vector<KanbanSource>& sources,
    std::vector<std::string>& warnings)
{
    const auto normalized_repository = normalized_path(repository_root);
    if (!visited_repositories.insert(normalized_repository).second)
        return;

    const auto modules_path = normalized_repository / ".gitmodules";
    std::error_code ec;
    if (!std::filesystem::exists(modules_path, ec))
        return;
    if (ec)
    {
        warnings.push_back("failed to inspect " + modules_path.string() + ": " + ec.message());
        return;
    }

    std::ifstream modules(modules_path);
    if (!modules)
    {
        warnings.push_back("failed to open " + modules_path.string());
        return;
    }

    std::string line;
    while (std::getline(modules, line))
    {
        const auto stripped = trim(line);
        const auto equals = stripped.find('=');
        if (equals == std::string::npos
            || trim(std::string_view(stripped).substr(0, equals)) != "path")
        {
            continue;
        }

        const auto configured_path = std::filesystem::path(
            unquote_path(stripped.substr(equals + 1)));
        if (configured_path.empty() || configured_path.is_absolute())
        {
            warnings.push_back("ignored invalid submodule path in " + modules_path.string());
            continue;
        }

        const auto submodule_root = normalized_path(
            normalized_repository / configured_path);
        if (!path_is_within(workspace_root, submodule_root))
        {
            warnings.push_back("ignored submodule path outside workspace: "
                + submodule_root.string());
            continue;
        }

        const bool submodule_exists = std::filesystem::exists(submodule_root, ec);
        if (ec)
        {
            warnings.push_back("failed to inspect submodule "
                + submodule_root.string() + ": " + ec.message());
            ec.clear();
            continue;
        }
        if (!submodule_exists || !std::filesystem::is_directory(submodule_root, ec))
        {
            if (ec)
            {
                warnings.push_back("failed to inspect submodule "
                    + submodule_root.string() + ": " + ec.message());
                ec.clear();
            }
            continue;
        }

        const auto board_root = submodule_root / "kanban";
        const bool board_exists = std::filesystem::exists(board_root, ec);
        if (ec)
        {
            warnings.push_back("failed to inspect submodule board "
                + board_root.string() + ": " + ec.message());
            ec.clear();
        }
        else if (board_exists && std::filesystem::is_directory(board_root, ec) && !ec)
        {
            sources.push_back(KanbanSource{
                .name = submodule_root.filename().string(),
                .root = normalized_path(board_root),
            });
        }
        else if (ec)
        {
            warnings.push_back("failed to inspect submodule board "
                + board_root.string() + ": " + ec.message());
            ec.clear();
        }

        discover_submodule_boards(
            submodule_root,
            workspace_root,
            visited_repositories,
            sources,
            warnings);
        ec.clear();
    }
}

KanbanColumn* find_column(KanbanBoard& board, std::string_view name)
{
    const auto it = std::ranges::find(board.columns, name, &KanbanColumn::name);
    return it == board.columns.end() ? nullptr : &*it;
}

std::string unique_source_name(
    std::string preferred,
    const std::filesystem::path& repository_root,
    const std::filesystem::path& workspace_root,
    const std::vector<KanbanSource>& existing)
{
    if (std::ranges::none_of(existing, [&](const auto& source) {
            return source.name == preferred;
        }))
    {
        return preferred;
    }

    auto relative = repository_root.lexically_relative(workspace_root).generic_string();
    if (relative.empty() || relative == ".")
        relative = preferred;
    return relative;
}

} // namespace

std::shared_ptr<KanbanDirectoryOperations>
native_kanban_directory_operations()
{
    static auto operations
        = std::make_shared<NativeKanbanDirectoryOperations>();
    return operations;
}

ScopedKanbanDirectoryOperationsOverride::
    ScopedKanbanDirectoryOperationsOverride(
        KanbanDirectoryOperations& operations)
    : previous_(directory_operations_override)
{
    directory_operations_override = &operations;
}

ScopedKanbanDirectoryOperationsOverride::
    ~ScopedKanbanDirectoryOperationsOverride()
{
    directory_operations_override = previous_;
}

std::filesystem::path resolve_kanban_root(
    const std::filesystem::path& source_path,
    const std::filesystem::path& working_dir,
    std::string* error)
{
    clear_error(error);

    std::filesystem::path root;
    if (source_path.empty())
    {
        const auto base = working_dir.empty() ? std::filesystem::current_path() : working_dir;
        root = base / "kanban";
    }
    else if (source_path.is_relative() && !working_dir.empty())
    {
        root = working_dir / source_path;
    }
    else
    {
        root = source_path;
    }

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        set_error(error, "failed to create kanban root: " + ec.message());
        return {};
    }

    if (std::filesystem::is_empty(root, ec) && !ec)
    {
        std::filesystem::create_directories(root / "ice-box", ec);
        std::filesystem::create_directories(root / "pending", ec);
        std::filesystem::create_directories(root / "done", ec);
        if (ec)
        {
            set_error(error, "failed to create default kanban columns: " + ec.message());
            return {};
        }
    }

    auto canonical = std::filesystem::weakly_canonical(root, ec);
    if (ec)
    {
        canonical = std::filesystem::absolute(root, ec);
    }
    return ec ? root : canonical;
}

KanbanBoard load_kanban_board(const std::filesystem::path& root, std::string* error)
{
    clear_error(error);

    KanbanBoard board;
    board.root = root;
    const auto repository_root = root.parent_path();
    board.sources.push_back(KanbanSource{
        .name = repository_root.filename().string(),
        .root = root,
    });

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
    {
        set_error(error, "kanban root is not a directory: " + root.string());
        return board;
    }

    OrderedNames column_names;
    auto root_it = active_directory_operations().open(root, ec);
    if (ec || !root_it)
    {
        if (!ec)
            ec = std::make_error_code(std::errc::io_error);
        set_error(error, "failed to scan kanban root: " + ec.message());
        return board;
    }
    while (!root_it->at_end())
    {
        const auto entry = root_it->entry();
        if (!root_it->increment(ec))
        {
            set_error(error, "failed to scan kanban root: " + ec.message());
            return board;
        }
        const bool is_directory = entry.is_directory(ec);
        if (ec)
        {
            set_error(error, "failed to inspect kanban root entry: " + ec.message());
            return board;
        }
        if (!is_directory)
        {
            continue;
        }
        const auto name = entry.path().filename().string();
        if (name.starts_with('.'))
        {
            continue;
        }
        column_names.push_back(name);
    }

    sort_columns_for_first_load(column_names);
    for (const auto& name : column_names)
    {
        KanbanColumn column;
        column.name = name;
        column.directory = root / name;

        auto column_it = active_directory_operations().open(
            column.directory, ec);
        if (ec || !column_it)
        {
            if (!ec)
                ec = std::make_error_code(std::errc::io_error);
            set_error(error, "failed to scan kanban column: " + ec.message());
            return board;
        }
        while (!column_it->at_end())
        {
            const auto entry = column_it->entry();
            if (!column_it->increment(ec))
            {
                set_error(error, "failed to scan kanban column: " + ec.message());
                return board;
            }
            const bool is_regular_file = entry.is_regular_file(ec);
            if (ec)
            {
                set_error(error, "failed to inspect kanban column entry: " + ec.message());
                return board;
            }
            if (!is_regular_file || entry.path().extension() != ".md")
            {
                continue;
            }
            const auto file_name = entry.path().filename().string();
            column.cards.push_back(KanbanCard{
                .file_name = file_name,
                .path = entry.path(),
                .kind = card_kind_for_file(file_name),
                .source_index = 0,
                .source_name = board.sources.front().name,
                .source_root = root,
            });
        }

        std::ranges::sort(column.cards, {}, &KanbanCard::file_name);
        board.columns.push_back(std::move(column));
    }

    const auto metadata = read_metadata(root, error);
    if (metadata)
    {
        apply_column_metadata(board.columns, metadata->columns);
        for (auto& column : board.columns)
        {
            const auto it = metadata->cards_by_column.find(column.name);
            if (it != metadata->cards_by_column.end())
            {
                apply_card_metadata(column, it->second);
            }
        }
    }

    return board;
}

KanbanBoard load_kanban_workspace(
    const std::filesystem::path& root,
    std::string* error)
{
    clear_error(error);

    KanbanBoard workspace;
    workspace.root = root;

    const auto repository_root = normalized_path(root.parent_path());
    std::vector<KanbanSource> discovered{
        KanbanSource{
            .name = repository_root.filename().string(),
            .root = normalized_path(root),
        },
    };
    std::set<std::filesystem::path> visited_repositories;
    discover_submodule_boards(
        repository_root,
        repository_root,
        visited_repositories,
        discovered,
        workspace.warnings);

    std::set<std::filesystem::path> visited_boards;
    for (auto source : discovered)
    {
        source.root = normalized_path(source.root);
        if (!visited_boards.insert(source.root).second)
            continue;

        source.name = unique_source_name(
            source.name,
            source.root.parent_path(),
            repository_root,
            workspace.sources);

        std::string source_error;
        auto local = load_kanban_board(source.root, &source_error);
        if (!source_error.empty())
        {
            if (workspace.sources.empty())
            {
                set_error(error, std::move(source_error));
                return workspace;
            }
            workspace.warnings.push_back(
                source.name + ": " + source_error);
            continue;
        }

        const size_t source_index = workspace.sources.size();
        workspace.sources.push_back(source);
        for (auto& local_column : local.columns)
        {
            auto* destination = find_column(workspace, local_column.name);
            if (!destination)
            {
                workspace.columns.push_back(KanbanColumn{
                    .name = local_column.name,
                    .directory = workspace.root / local_column.name,
                });
                destination = &workspace.columns.back();
            }

            for (auto& card : local_column.cards)
            {
                card.source_index = source_index;
                card.source_name = source.name;
                card.source_root = source.root;
                destination->cards.push_back(std::move(card));
            }
        }
    }

    return workspace;
}

bool save_kanban_order(const KanbanBoard& board, std::string* error)
{
    clear_error(error);

    if (board.sources.size() > 1)
    {
        set_error(error,
            "aggregate kanban order must be saved for one source at a time");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(board.root, ec);
    if (ec)
    {
        set_error(error, "failed to create kanban root: " + ec.message());
        return false;
    }

    const auto path = board.root / std::string(kKanbanMetadataFileName);
    const auto temp_path = board.root / (std::string(kKanbanMetadataFileName) + ".tmp");
    const auto backup_path = board.root / (std::string(kKanbanMetadataFileName) + ".bak");

    std::filesystem::remove(temp_path, ec);
    if (ec)
    {
        set_error(error, "failed to remove stale kanban metadata temp file: " + ec.message());
        return false;
    }

    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        set_error(error, "failed to write kanban metadata: " + temp_path.string());
        return false;
    }

    OrderedNames column_names;
    for (const auto& column : board.columns)
    {
        column_names.push_back(column.name);
    }

    file << "version = 1\n";
    file << "columns = ";
    write_string_array(file, column_names);
    file << "\n[cards]\n";
    for (const auto& column : board.columns)
    {
        OrderedNames card_names;
        for (const auto& card : column.cards)
        {
            card_names.push_back(card.file_name);
        }
        file << column.name << " = ";
        write_string_array(file, card_names);
        file << "\n";
    }

    file.close();
    if (!file)
    {
        set_error(error, "failed to flush kanban metadata: " + temp_path.string());
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    std::filesystem::remove(backup_path, ec);
    if (ec)
    {
        set_error(error, "failed to remove stale kanban metadata backup file: " + ec.message());
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    bool has_backup = false;
    if (std::filesystem::exists(path, ec))
    {
        std::filesystem::rename(path, backup_path, ec);
        if (ec)
        {
            set_error(error, "failed to stage existing kanban metadata backup: " + ec.message());
            std::filesystem::remove(temp_path, ec);
            return false;
        }
        has_backup = true;
    }
    else if (ec)
    {
        set_error(error, "failed to check kanban metadata target: " + ec.message());
        std::filesystem::remove(temp_path, ec);
        return false;
    }

    std::filesystem::rename(temp_path, path, ec);
    if (ec)
    {
        set_error(error, "failed to replace kanban metadata: " + ec.message());
        std::filesystem::remove(temp_path, ec);
        if (has_backup)
        {
            std::error_code restore_ec;
            std::filesystem::rename(backup_path, path, restore_ec);
        }
        return false;
    }

    if (has_backup)
    {
        std::filesystem::remove(backup_path, ec);
    }

    return true;
}

bool save_kanban_order_for_source(
    const KanbanBoard& board,
    size_t source_index,
    std::string* error)
{
    clear_error(error);
    if (source_index >= board.sources.size())
    {
        set_error(error, "kanban source index is out of range");
        return false;
    }

    const auto& source = board.sources[source_index];
    KanbanBoard local;
    local.root = source.root;
    local.sources.push_back(source);

    std::error_code ec;
    for (const auto& column : board.columns)
    {
        const auto directory = source.root / column.name;
        KanbanColumn local_column{
            .name = column.name,
            .directory = directory,
        };
        for (const auto& card : column.cards)
        {
            if (card.source_index == source_index)
                local_column.cards.push_back(card);
        }

        const bool directory_exists = std::filesystem::is_directory(directory, ec);
        if (ec)
        {
            set_error(error, "failed to inspect kanban source column: " + ec.message());
            return false;
        }
        if (directory_exists || !local_column.cards.empty())
            local.columns.push_back(std::move(local_column));
    }

    return save_kanban_order(local, error);
}

bool reorder_card(KanbanBoard& board, KanbanSelection selection, int row_delta, std::string* error)
{
    clear_error(error);
    if (!selection_has_card(board, selection))
    {
        set_error(error, "kanban selection does not reference a card");
        return false;
    }

    auto& cards = board.columns[static_cast<size_t>(selection.column)].cards;
    const auto card_index = static_cast<size_t>(selection.card);
    const size_t source_index = cards[card_index].source_index;

    std::vector<size_t> source_cards;
    for (size_t index = 0; index < cards.size(); ++index)
    {
        if (cards[index].source_index == source_index)
            source_cards.push_back(index);
    }

    const auto source_position = std::ranges::find(source_cards, card_index);
    if (source_position == source_cards.end())
    {
        set_error(error, "kanban source group does not contain selected card");
        return false;
    }
    const auto position = static_cast<int64_t>(
        std::distance(source_cards.begin(), source_position));
    const auto target_position = std::clamp(
        position + static_cast<int64_t>(row_delta),
        int64_t{ 0 },
        static_cast<int64_t>(source_cards.size()) - 1);
    if (position != target_position)
    {
        std::swap(
            cards[source_cards[static_cast<size_t>(position)]],
            cards[source_cards[static_cast<size_t>(target_position)]]);
    }
    return true;
}

bool move_card_to_column(
    KanbanBoard& board,
    KanbanSelection selection,
    int target_column,
    std::string* error)
{
    clear_error(error);
    if (!selection_has_card(board, selection))
    {
        set_error(error, "kanban selection does not reference a card");
        return false;
    }
    if (board.columns.empty())
    {
        set_error(error, "kanban board has no columns");
        return false;
    }

    target_column = std::clamp(target_column, 0, static_cast<int>(board.columns.size()) - 1);
    if (target_column == selection.column)
    {
        return true;
    }

    auto& source_column = board.columns[static_cast<size_t>(selection.column)];
    auto& destination_column = board.columns[static_cast<size_t>(target_column)];
    const auto source_index = static_cast<size_t>(selection.card);
    KanbanCard moving = source_column.cards[source_index];
    size_t source_position = 0;
    for (size_t index = 0; index < source_index; ++index)
    {
        if (source_column.cards[index].source_index == moving.source_index)
            ++source_position;
    }
    const auto owner_root = moving.source_root.empty()
        ? board.root
        : moving.source_root;
    const auto destination_directory = owner_root / destination_column.name;
    const auto destination_path = destination_directory / moving.file_name;

    std::error_code ec;
    if (std::filesystem::exists(destination_path, ec))
    {
        set_error(error, "destination kanban card already exists: " + destination_path.string());
        return false;
    }

    std::filesystem::create_directories(destination_directory, ec);
    if (ec)
    {
        set_error(error, "failed to create destination kanban column: " + ec.message());
        return false;
    }

    std::filesystem::rename(moving.path, destination_path, ec);
    if (ec)
    {
        set_error(error, "failed to move kanban card: " + ec.message());
        return false;
    }

    moving.path = destination_path;
    source_column.cards.erase(source_column.cards.begin() + static_cast<std::ptrdiff_t>(source_index));
    const auto group_begin = std::ranges::find_if(
        destination_column.cards,
        [&](const KanbanCard& card) {
            return card.source_index >= moving.source_index;
        });
    const auto group_end = std::ranges::find_if(
        group_begin,
        destination_column.cards.end(),
        [&](const KanbanCard& card) {
            return card.source_index > moving.source_index;
        });
    const size_t group_size = static_cast<size_t>(
        std::distance(group_begin, group_end));
    const auto insert = std::next(
        group_begin,
        static_cast<std::ptrdiff_t>(std::min(source_position, group_size)));
    const auto insert_index = static_cast<size_t>(
        std::distance(destination_column.cards.begin(), insert));
    destination_column.cards.insert(
        destination_column.cards.begin() + static_cast<std::ptrdiff_t>(insert_index),
        std::move(moving));
    return true;
}

bool delete_card(
    KanbanBoard& board,
    KanbanSelection selection,
    std::string* error)
{
    clear_error(error);
    if (!selection_has_card(board, selection))
    {
        set_error(error, "kanban selection does not reference a card");
        return false;
    }

    auto& cards = board.columns[static_cast<size_t>(selection.column)].cards;
    const auto card_index = static_cast<size_t>(selection.card);
    const auto path = cards[card_index].path;

    std::error_code ec;
    const bool removed = std::filesystem::remove(path, ec);
    if (ec)
    {
        set_error(error, "failed to delete kanban card: " + ec.message());
        return false;
    }
    if (!removed)
    {
        set_error(error, "kanban card does not exist: " + path.string());
        return false;
    }

    cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(card_index));
    return true;
}

} // namespace draxul::kanban
