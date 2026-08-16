#include <draxul/kanban/kanban_store.h>

#include <draxul/string_util.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>

namespace draxul::kanban
{
namespace
{
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

} // namespace

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

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
    {
        set_error(error, "kanban root is not a directory: " + root.string());
        return board;
    }

    OrderedNames column_names;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
        {
            set_error(error, "failed to scan kanban root: " + ec.message());
            return board;
        }
        if (!entry.is_directory(ec))
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

        for (const auto& entry : std::filesystem::directory_iterator(column.directory, ec))
        {
            if (ec)
            {
                set_error(error, "failed to scan kanban column: " + ec.message());
                return board;
            }
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".md")
            {
                continue;
            }
            const auto file_name = entry.path().filename().string();
            column.cards.push_back(KanbanCard{
                .file_name = file_name,
                .path = entry.path(),
                .kind = card_kind_for_file(file_name),
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

bool save_kanban_order(const KanbanBoard& board, std::string* error)
{
    clear_error(error);

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

bool reorder_card(KanbanBoard& board, KanbanSelection selection, int row_delta, std::string* error)
{
    clear_error(error);
    if (!selection_has_card(board, selection))
    {
        set_error(error, "kanban selection does not reference a card");
        return false;
    }

    auto& cards = board.columns[static_cast<size_t>(selection.column)].cards;
    const auto source = static_cast<int64_t>(selection.card);
    const auto target = std::clamp(
        source + static_cast<int64_t>(row_delta),
        int64_t{ 0 },
        static_cast<int64_t>(cards.size()) - 1);
    if (source != target)
    {
        std::swap(cards[static_cast<size_t>(source)], cards[static_cast<size_t>(target)]);
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
    const auto destination_path = destination_column.directory / moving.file_name;

    std::error_code ec;
    if (std::filesystem::exists(destination_path, ec))
    {
        set_error(error, "destination kanban card already exists: " + destination_path.string());
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
    const auto insert_index = std::min(
        static_cast<size_t>(selection.card),
        destination_column.cards.size());
    destination_column.cards.insert(
        destination_column.cards.begin() + static_cast<std::ptrdiff_t>(insert_index),
        std::move(moving));
    return true;
}

} // namespace draxul::kanban
