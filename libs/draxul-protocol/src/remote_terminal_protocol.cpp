#include <draxul/remote_terminal_protocol.h>

#include <cmath>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>

namespace draxul
{

namespace
{

nlohmann::json color_to_json(const Color& color)
{
    return nlohmann::json::array({ color.r, color.g, color.b, color.a });
}

bool read_color(const nlohmann::json& value, Color& color)
{
    if (!value.is_array() || value.size() != 4)
        return false;
    for (const auto& component : value)
    {
        if (!component.is_number())
            return false;
    }
    color = Color(value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>());
    return std::isfinite(color.r) && std::isfinite(color.g)
        && std::isfinite(color.b) && std::isfinite(color.a);
}

nlohmann::json attr_to_json(const HlAttr& attr)
{
    return {
        { "fg", color_to_json(attr.fg) },
        { "bg", color_to_json(attr.bg) },
        { "sp", color_to_json(attr.sp) },
        { "has_fg", attr.has_fg },
        { "has_bg", attr.has_bg },
        { "has_sp", attr.has_sp },
        { "bold", attr.bold },
        { "italic", attr.italic },
        { "underline", attr.underline },
        { "undercurl", attr.undercurl },
        { "strikethrough", attr.strikethrough },
        { "reverse", attr.reverse },
    };
}

bool read_bool(const nlohmann::json& value, std::string_view key, bool& target)
{
    if (!value.contains(key) || !value[key].is_boolean())
        return false;
    target = value[key].get<bool>();
    return true;
}

bool read_attr(const nlohmann::json& value, HlAttr& attr)
{
    return value.is_object()
        && value.contains("fg") && read_color(value["fg"], attr.fg)
        && value.contains("bg") && read_color(value["bg"], attr.bg)
        && value.contains("sp") && read_color(value["sp"], attr.sp)
        && read_bool(value, "has_fg", attr.has_fg)
        && read_bool(value, "has_bg", attr.has_bg)
        && read_bool(value, "has_sp", attr.has_sp)
        && read_bool(value, "bold", attr.bold)
        && read_bool(value, "italic", attr.italic)
        && read_bool(value, "underline", attr.underline)
        && read_bool(value, "undercurl", attr.undercurl)
        && read_bool(value, "strikethrough", attr.strikethrough)
        && read_bool(value, "reverse", attr.reverse);
}

nlohmann::json cell_to_json(const TerminalCellSnapshot& cell)
{
    return {
        { "text", cell.text },
        { "attr", attr_to_json(cell.attr) },
        { "double_width", cell.double_width },
        { "double_width_continuation", cell.double_width_continuation },
        { "hyperlink", cell.hyperlink },
    };
}

bool read_cell(const nlohmann::json& value, TerminalCellSnapshot& cell)
{
    if (!value.is_object()
        || !value.contains("text") || !value["text"].is_string()
        || !value.contains("attr") || !read_attr(value["attr"], cell.attr)
        || !read_bool(value, "double_width", cell.double_width)
        || !read_bool(value, "double_width_continuation",
            cell.double_width_continuation)
        || !value.contains("hyperlink") || !value["hyperlink"].is_string())
    {
        return false;
    }
    cell.text = value["text"].get<std::string>();
    cell.hyperlink = value["hyperlink"].get<std::string>();
    return cell.text.size() <= TerminalStateLimits::kMaxCellTextBytes
        && cell.hyperlink.size() <= TerminalStateLimits::kMaxHyperlinkBytes;
}

class CompactCellEncoder
{
public:
    nlohmann::json encode(const TerminalCellSnapshot& cell)
    {
        auto [it, inserted] = attr_ids_.try_emplace(
            cell.attr, attr_ids_.size());
        if (inserted)
            attrs_.push_back(attr_to_json(cell.attr));
        return nlohmann::json::array({
            cell.text,
            it->second,
            cell.double_width,
            cell.double_width_continuation,
            cell.hyperlink,
        });
    }

    nlohmann::json take_attrs()
    {
        return std::move(attrs_);
    }

private:
    std::unordered_map<HlAttr, size_t, HlAttrHash> attr_ids_;
    nlohmann::json attrs_ = nlohmann::json::array();
};

bool read_attr_table(const nlohmann::json& value,
    std::vector<HlAttr>& attrs)
{
    if (!value.is_array()
        || value.size() > TerminalStateLimits::kMaxCells)
    {
        return false;
    }
    attrs.reserve(value.size());
    for (const auto& item : value)
    {
        HlAttr attr;
        if (!read_attr(item, attr))
            return false;
        attrs.push_back(attr);
    }
    return true;
}

bool read_compact_cell(const nlohmann::json& value, size_t offset,
    const std::vector<HlAttr>& attrs, TerminalCellSnapshot& cell)
{
    if (!value.is_array() || value.size() != offset + 5
        || !value[offset].is_string()
        || !value[offset + 1].is_number_integer()
        || !value[offset + 2].is_boolean()
        || !value[offset + 3].is_boolean()
        || !value[offset + 4].is_string())
    {
        return false;
    }
    const int64_t attr_id = value[offset + 1].get<int64_t>();
    if (attr_id < 0 || static_cast<size_t>(attr_id) >= attrs.size())
        return false;
    cell.text = value[offset].get<std::string>();
    cell.attr = attrs[static_cast<size_t>(attr_id)];
    cell.double_width = value[offset + 2].get<bool>();
    cell.double_width_continuation = value[offset + 3].get<bool>();
    cell.hyperlink = value[offset + 4].get<std::string>();
    return cell.text.size() <= TerminalStateLimits::kMaxCellTextBytes
        && cell.hyperlink.size() <= TerminalStateLimits::kMaxHyperlinkBytes;
}

nlohmann::json metadata_to_json(const TerminalSnapshotMetadata& metadata)
{
    nlohmann::json marks = nlohmann::json::array();
    for (const auto& mark : metadata.shell_marks)
    {
        marks.push_back({
            { "kind", static_cast<int>(mark.kind) },
            { "row", mark.row },
            { "exit_code", mark.exit_code },
        });
    }
    return {
        { "cursor", {
              { "col", metadata.cursor.col },
              { "row", metadata.cursor.row },
              { "visible", metadata.cursor.visible },
              { "shape", static_cast<int>(metadata.cursor.shape) },
              { "blink", metadata.cursor.blink },
          } },
        { "modes", {
              { "alternate_screen", metadata.modes.alternate_screen },
              { "auto_wrap", metadata.modes.auto_wrap },
              { "origin", metadata.modes.origin },
              { "cursor_application", metadata.modes.cursor_application },
              { "bracketed_paste", metadata.modes.bracketed_paste },
              { "focus_reporting", metadata.modes.focus_reporting },
              { "synchronized_output", metadata.modes.synchronized_output },
              { "mouse", {
                    { "normal_tracking", metadata.modes.mouse.normal_tracking },
                    { "button_motion", metadata.modes.mouse.button_motion },
                    { "any_motion", metadata.modes.mouse.any_motion },
                    { "sgr_coordinates", metadata.modes.mouse.sgr_coordinates },
                } },
          } },
        { "title", metadata.title },
        { "working_directory", metadata.working_directory },
        { "shell_marks", std::move(marks) },
    };
}

bool read_metadata(const nlohmann::json& value,
    TerminalSnapshotMetadata& metadata)
{
    if (!value.is_object()
        || !value.contains("cursor") || !value["cursor"].is_object()
        || !value.contains("modes") || !value["modes"].is_object()
        || !value.contains("title") || !value["title"].is_string()
        || !value.contains("working_directory")
        || !value["working_directory"].is_string()
        || !value.contains("shell_marks") || !value["shell_marks"].is_array())
    {
        return false;
    }
    const auto& cursor = value["cursor"];
    const auto& modes = value["modes"];
    if (!cursor.contains("col") || !cursor["col"].is_number_integer()
        || !cursor.contains("row") || !cursor["row"].is_number_integer()
        || !cursor.contains("shape") || !cursor["shape"].is_number_integer()
        || !read_bool(cursor, "visible", metadata.cursor.visible)
        || !read_bool(cursor, "blink", metadata.cursor.blink)
        || !modes.contains("mouse") || !modes["mouse"].is_object()
        || !read_bool(modes, "alternate_screen",
            metadata.modes.alternate_screen)
        || !read_bool(modes, "auto_wrap", metadata.modes.auto_wrap)
        || !read_bool(modes, "origin", metadata.modes.origin)
        || !read_bool(modes, "cursor_application",
            metadata.modes.cursor_application)
        || !read_bool(modes, "bracketed_paste",
            metadata.modes.bracketed_paste)
        || !read_bool(modes, "focus_reporting",
            metadata.modes.focus_reporting)
        || !read_bool(modes, "synchronized_output",
            metadata.modes.synchronized_output)
        || !read_bool(modes["mouse"], "normal_tracking",
            metadata.modes.mouse.normal_tracking)
        || !read_bool(modes["mouse"], "button_motion",
            metadata.modes.mouse.button_motion)
        || !read_bool(modes["mouse"], "any_motion",
            metadata.modes.mouse.any_motion)
        || !read_bool(modes["mouse"], "sgr_coordinates",
            metadata.modes.mouse.sgr_coordinates))
    {
        return false;
    }

    metadata.cursor.col = cursor["col"].get<int>();
    metadata.cursor.row = cursor["row"].get<int>();
    const int shape = cursor["shape"].get<int>();
    if (shape < static_cast<int>(CursorShape::Block)
        || shape > static_cast<int>(CursorShape::Vertical))
    {
        return false;
    }
    metadata.cursor.shape = static_cast<CursorShape>(shape);
    metadata.title = value["title"].get<std::string>();
    metadata.working_directory
        = value["working_directory"].get<std::string>();
    if (metadata.title.size() > TerminalStateLimits::kMaxTitleBytes
        || metadata.working_directory.size()
            > TerminalStateLimits::kMaxWorkingDirectoryBytes
        || value["shell_marks"].size()
            > TerminalStateLimits::kMaxScrollbackRows)
    {
        return false;
    }
    for (const auto& item : value["shell_marks"])
    {
        if (!item.is_object()
            || !item.contains("kind") || !item["kind"].is_number_integer()
            || !item.contains("row") || !item["row"].is_number_integer()
            || !item.contains("exit_code")
            || !item["exit_code"].is_number_integer())
        {
            return false;
        }
        const int kind = item["kind"].get<int>();
        if (kind < static_cast<int>(TerminalShellMarkKind::PromptStart)
            || kind > static_cast<int>(TerminalShellMarkKind::OutputEnd))
        {
            return false;
        }
        metadata.shell_marks.push_back({
            .kind = static_cast<TerminalShellMarkKind>(kind),
            .row = item["row"].get<int>(),
            .exit_code = item["exit_code"].get<int>(),
        });
    }
    return true;
}

bool valid_dimensions(int cols, int rows)
{
    if (cols <= 0 || rows <= 0
        || cols > TerminalStateLimits::kMaxColumns
        || rows > TerminalStateLimits::kMaxRows)
    {
        return false;
    }
    return static_cast<size_t>(cols) * static_cast<size_t>(rows)
        <= TerminalStateLimits::kMaxCells;
}

nlohmann::json version_to_json(const RemoteTerminalVersion& version)
{
    return {
        { "server_epoch", version.server_epoch },
        { "terminal_id", version.terminal_id },
        { "generation", version.generation },
        { "sequence", version.sequence },
    };
}

bool read_version(const nlohmann::json& value,
    RemoteTerminalVersion& version)
{
    if (!value.is_object()
        || !value.contains("server_epoch")
        || !value["server_epoch"].is_string()
        || !value.contains("terminal_id")
        || !value["terminal_id"].is_string()
        || !value.contains("generation")
        || !value["generation"].is_number_unsigned()
        || !value.contains("sequence")
        || !value["sequence"].is_number_unsigned())
    {
        return false;
    }
    version.server_epoch = value["server_epoch"].get<std::string>();
    version.terminal_id = value["terminal_id"].get<std::string>();
    version.generation = value["generation"].get<uint64_t>();
    version.sequence = value["sequence"].get<uint64_t>();
    return !version.server_epoch.empty() && !version.terminal_id.empty()
        && version.server_epoch.size() <= 128
        && version.terminal_id.size() <= 128
        && version.generation > 0;
}

} // namespace

std::string_view to_string(RemoteTerminalEventKind kind)
{
    switch (kind)
    {
    case RemoteTerminalEventKind::Snapshot:
        return "snapshot";
    case RemoteTerminalEventKind::Delta:
        return "delta";
    case RemoteTerminalEventKind::Controller:
        return "controller";
    case RemoteTerminalEventKind::Clipboard:
        return "clipboard";
    }
    return "unknown";
}

std::optional<RemoteTerminalEventKind> parse_remote_terminal_event_kind(
    std::string_view value)
{
    if (value == "snapshot")
        return RemoteTerminalEventKind::Snapshot;
    if (value == "delta")
        return RemoteTerminalEventKind::Delta;
    if (value == "controller")
        return RemoteTerminalEventKind::Controller;
    if (value == "clipboard")
        return RemoteTerminalEventKind::Clipboard;
    return std::nullopt;
}

nlohmann::json terminal_semantic_snapshot_to_json(
    const TerminalSemanticSnapshot& snapshot)
{
    CompactCellEncoder encoder;
    nlohmann::json cells = nlohmann::json::array();
    for (const auto& cell : snapshot.cells)
        cells.push_back(encoder.encode(cell));
    return {
        { "cols", snapshot.cols },
        { "rows", snapshot.rows },
        { "attrs", encoder.take_attrs() },
        { "cells", std::move(cells) },
        { "metadata", metadata_to_json(snapshot.metadata) },
    };
}

std::optional<TerminalSemanticSnapshot>
terminal_semantic_snapshot_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object()
        || !value.contains("cols") || !value["cols"].is_number_integer()
        || !value.contains("rows") || !value["rows"].is_number_integer()
        || !value.contains("cells") || !value["cells"].is_array()
        || !value.contains("metadata"))
    {
        error = "Terminal snapshot is invalid.";
        return std::nullopt;
    }
    TerminalSemanticSnapshot snapshot;
    snapshot.cols = value["cols"].get<int>();
    snapshot.rows = value["rows"].get<int>();
    std::vector<HlAttr> attrs;
    const bool compact = value.contains("attrs");
    if (!valid_dimensions(snapshot.cols, snapshot.rows)
        || value["cells"].size()
            != static_cast<size_t>(snapshot.cols) * snapshot.rows
        || (compact && !read_attr_table(value["attrs"], attrs))
        || !read_metadata(value["metadata"], snapshot.metadata))
    {
        error = "Terminal snapshot values are out of range.";
        return std::nullopt;
    }
    snapshot.cells.reserve(value["cells"].size());
    for (const auto& item : value["cells"])
    {
        TerminalCellSnapshot cell;
        if (!(compact
                ? read_compact_cell(item, 0, attrs, cell)
                : read_cell(item, cell)))
        {
            error = "Terminal snapshot contains an invalid cell.";
            return std::nullopt;
        }
        snapshot.cells.push_back(std::move(cell));
    }
    return snapshot;
}

nlohmann::json terminal_dirty_snapshot_to_json(
    const TerminalDirtySnapshot& snapshot)
{
    CompactCellEncoder encoder;
    nlohmann::json cells = nlohmann::json::array();
    for (const auto& item : snapshot.cells)
    {
        auto cell = encoder.encode(item.cell);
        cell.insert(cell.begin(), item.row);
        cell.insert(cell.begin(), item.col);
        cells.push_back(std::move(cell));
    }
    return {
        { "cols", snapshot.cols },
        { "rows", snapshot.rows },
        { "full", snapshot.full },
        { "attrs", encoder.take_attrs() },
        { "cells", std::move(cells) },
        { "metadata", metadata_to_json(snapshot.metadata) },
    };
}

std::optional<TerminalDirtySnapshot> terminal_dirty_snapshot_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object()
        || !value.contains("cols") || !value["cols"].is_number_integer()
        || !value.contains("rows") || !value["rows"].is_number_integer()
        || !value.contains("full") || !value["full"].is_boolean()
        || !value.contains("cells") || !value["cells"].is_array()
        || !value.contains("metadata"))
    {
        error = "Terminal delta is invalid.";
        return std::nullopt;
    }
    TerminalDirtySnapshot snapshot;
    snapshot.cols = value["cols"].get<int>();
    snapshot.rows = value["rows"].get<int>();
    snapshot.full = value["full"].get<bool>();
    std::vector<HlAttr> attrs;
    const bool compact = value.contains("attrs");
    if (!valid_dimensions(snapshot.cols, snapshot.rows)
        || value["cells"].size() > TerminalStateLimits::kMaxCells
        || (compact && !read_attr_table(value["attrs"], attrs))
        || !read_metadata(value["metadata"], snapshot.metadata))
    {
        error = "Terminal delta values are out of range.";
        return std::nullopt;
    }
    snapshot.cells.reserve(value["cells"].size());
    for (const auto& item : value["cells"])
    {
        TerminalDirtyCellSnapshot dirty;
        if (compact)
        {
            if (!item.is_array() || item.size() != 7
                || !item[0].is_number_integer()
                || !item[1].is_number_integer())
            {
                error = "Terminal delta contains an invalid cell.";
                return std::nullopt;
            }
            dirty.col = item[0].get<int>();
            dirty.row = item[1].get<int>();
        }
        else
        {
            if (!item.is_object()
                || !item.contains("col")
                || !item["col"].is_number_integer()
                || !item.contains("row")
                || !item["row"].is_number_integer()
                || !item.contains("cell"))
            {
                error = "Terminal delta contains an invalid cell.";
                return std::nullopt;
            }
            dirty.col = item["col"].get<int>();
            dirty.row = item["row"].get<int>();
        }
        if (dirty.col < 0 || dirty.col >= snapshot.cols
            || dirty.row < 0 || dirty.row >= snapshot.rows
            || !(compact
                    ? read_compact_cell(item, 2, attrs, dirty.cell)
                    : read_cell(item["cell"], dirty.cell)))
        {
            error = "Terminal delta cell is out of range.";
            return std::nullopt;
        }
        snapshot.cells.push_back(std::move(dirty));
    }
    if (snapshot.full
        && snapshot.cells.size()
            != static_cast<size_t>(snapshot.cols) * snapshot.rows)
    {
        error = "Full terminal delta does not contain every cell.";
        return std::nullopt;
    }
    return snapshot;
}

nlohmann::json remote_terminal_event_to_json(
    const RemoteTerminalEvent& event)
{
    nlohmann::json value{
        { "kind", to_string(event.kind) },
        { "version", version_to_json(event.version) },
        { "controller_client_id", event.controller_client_id },
    };
    if (event.snapshot)
        value["snapshot"] = terminal_semantic_snapshot_to_json(*event.snapshot);
    if (event.delta)
        value["delta"] = terminal_dirty_snapshot_to_json(*event.delta);
    if (event.clipboard)
        value["clipboard"] = *event.clipboard;
    return value;
}

std::optional<RemoteTerminalEvent> remote_terminal_event_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object()
        || !value.contains("kind") || !value["kind"].is_string()
        || !value.contains("version")
        || !value.contains("controller_client_id")
        || !value["controller_client_id"].is_string())
    {
        error = "Remote terminal event is invalid.";
        return std::nullopt;
    }
    const auto kind = parse_remote_terminal_event_kind(
        value["kind"].get_ref<const std::string&>());
    RemoteTerminalEvent event;
    if (!kind || !read_version(value["version"], event.version))
    {
        error = "Remote terminal event version is invalid.";
        return std::nullopt;
    }
    event.kind = *kind;
    event.controller_client_id
        = value["controller_client_id"].get<std::string>();
    if (event.controller_client_id.size() > 128)
    {
        error = "Remote terminal controller id is invalid.";
        return std::nullopt;
    }
    if (event.kind == RemoteTerminalEventKind::Snapshot)
    {
        if (!value.contains("snapshot")
            || value.contains("delta") || value.contains("clipboard"))
        {
            error = "Remote terminal snapshot event has no snapshot.";
            return std::nullopt;
        }
        event.snapshot = terminal_semantic_snapshot_from_json(
            value["snapshot"], error);
        if (!event.snapshot)
            return std::nullopt;
    }
    else if (event.kind == RemoteTerminalEventKind::Delta)
    {
        if (!value.contains("delta")
            || value.contains("snapshot") || value.contains("clipboard"))
        {
            error = "Remote terminal delta event has no delta.";
            return std::nullopt;
        }
        event.delta = terminal_dirty_snapshot_from_json(
            value["delta"], error);
        if (!event.delta)
            return std::nullopt;
    }
    else if (event.kind == RemoteTerminalEventKind::Clipboard)
    {
        if (!value.contains("clipboard")
            || value.contains("snapshot") || value.contains("delta")
            || !value["clipboard"].is_string())
        {
            error = "Remote terminal clipboard event has no clipboard value.";
            return std::nullopt;
        }
        event.clipboard = value["clipboard"].get<std::string>();
        if (event.clipboard->size() > TerminalStateLimits::kMaxInputBytes)
        {
            error = "Remote terminal clipboard event is too large.";
            return std::nullopt;
        }
    }
    else if (value.contains("snapshot") || value.contains("delta")
        || value.contains("clipboard"))
    {
        error = "Remote terminal controller event contains state.";
        return std::nullopt;
    }
    return event;
}

nlohmann::json remote_terminal_attach_to_json(
    const RemoteTerminalAttach& attach)
{
    return {
        { "pane", {
              { "pane_id", attach.pane.pane_id },
              { "terminal_id", attach.pane.terminal_id },
              { "name", attach.pane.name },
              { "execution_domain", attach.pane.execution_domain },
              { "process_id", attach.pane.process_id },
          } },
        { "state", remote_terminal_event_to_json(attach.state) },
    };
}

std::optional<RemoteTerminalAttach> remote_terminal_attach_from_json(
    const nlohmann::json& value, std::string& error)
{
    if (!value.is_object() || !value.contains("pane")
        || !value["pane"].is_object() || !value.contains("state"))
    {
        error = "Remote terminal attach response is invalid.";
        return std::nullopt;
    }
    const auto& pane = value["pane"];
    for (std::string_view key :
        { "pane_id", "terminal_id", "name", "execution_domain" })
    {
        if (!pane.contains(key) || !pane[key].is_string())
        {
            error = "Remote pane descriptor is invalid.";
            return std::nullopt;
        }
    }
    RemoteTerminalAttach attach;
    attach.pane.pane_id = pane["pane_id"].get<std::string>();
    attach.pane.terminal_id = pane["terminal_id"].get<std::string>();
    attach.pane.name = pane["name"].get<std::string>();
    attach.pane.execution_domain
        = pane["execution_domain"].get<std::string>();
    if (pane.contains("process_id"))
    {
        if (!pane["process_id"].is_number_unsigned())
        {
            error = "Remote pane process identity is invalid.";
            return std::nullopt;
        }
        attach.pane.process_id = pane["process_id"].get<uint64_t>();
    }
    if (attach.pane.pane_id.empty() || attach.pane.terminal_id.empty()
        || attach.pane.pane_id.size() > 128
        || attach.pane.terminal_id.size() > 128
        || attach.pane.name.size() > 256
        || attach.pane.execution_domain != "server_terminal")
    {
        error = "Remote pane descriptor values are invalid.";
        return std::nullopt;
    }
    auto event = remote_terminal_event_from_json(value["state"], error);
    if (!event || event->kind != RemoteTerminalEventKind::Snapshot)
        return std::nullopt;
    attach.state = std::move(*event);
    return attach;
}

nlohmann::json remote_terminal_scrollback_page_to_json(
    const RemoteTerminalScrollbackPage& page)
{
    nlohmann::json value{
        { "version", version_to_json(page.version) },
        { "total_rows", page.total_rows },
        { "offset_from_live", page.offset_from_live },
        { "cols", page.cols },
    };
    if (page.snapshot)
        value["snapshot"] = terminal_semantic_snapshot_to_json(*page.snapshot);
    return value;
}

std::optional<RemoteTerminalScrollbackPage>
remote_terminal_scrollback_page_from_json(
    const nlohmann::json& value, std::string& error)
{
    RemoteTerminalScrollbackPage page;
    if (!value.is_object()
        || !value.contains("version")
        || !read_version(value["version"], page.version)
        || !value.contains("total_rows")
        || !value["total_rows"].is_number_unsigned()
        || !value.contains("offset_from_live")
        || !value["offset_from_live"].is_number_unsigned()
        || !value.contains("cols")
        || !value["cols"].is_number_integer())
    {
        error = "Remote terminal scrollback page is invalid.";
        return std::nullopt;
    }
    page.total_rows = value["total_rows"].get<uint64_t>();
    page.offset_from_live = value["offset_from_live"].get<uint64_t>();
    page.cols = value["cols"].get<int>();
    if (page.total_rows > TerminalStateLimits::kMaxScrollbackRows
        || page.offset_from_live > page.total_rows
        || page.cols <= 0
        || page.cols > TerminalStateLimits::kMaxColumns)
    {
        error = "Remote terminal scrollback page values are out of range.";
        return std::nullopt;
    }
    if (value.contains("snapshot"))
    {
        page.snapshot = terminal_semantic_snapshot_from_json(
            value["snapshot"], error);
        if (!page.snapshot
            || page.snapshot->cols != page.cols
            || page.snapshot->rows
                > static_cast<int>(kRemoteTerminalMaxScrollbackPageRows))
        {
            if (error.empty())
                error = "Remote terminal scrollback snapshot is invalid.";
            return std::nullopt;
        }
    }
    return page;
}

} // namespace draxul
