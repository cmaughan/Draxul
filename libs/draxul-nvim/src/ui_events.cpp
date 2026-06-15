#include <algorithm>
#include <array>
#include <cassert>
#include <exception>
#include <cstdio>
#include <draxul/log.h>
#include <draxul/nvim_ui.h>
#include <draxul/perf_timing.h>
#include <draxul/unicode.h>
#include <limits>
#include <memory>
#include <utility>

namespace draxul
{

namespace
{

// Maximum attr ID that fits in the internal uint16_t storage type.
constexpr int64_t kMaxAttrId = UINT16_MAX;

const MpackValue::ArrayStorage* try_get_array(const MpackValue& value)
{
    if (value.type() != MpackValue::Array)
        return nullptr;
    return &value.as_array();
}

const std::string* try_get_string(const MpackValue& value)
{
    if (value.type() != MpackValue::String)
        return nullptr;
    return &value.as_str();
}

template <typename Fn, typename... Args>
void invoke_callback(std::string_view name, const Fn& fn, Args&&... args)
{
    if (fn)
    {
        fn(std::forward<Args>(args)...);
        return;
    }

    DRAXUL_LOG_WARN(LogCategory::App,
        "UiEventHandler callback '%.*s' is unset",
        static_cast<int>(name.size()), name.data());
}

bool try_get_int(const MpackValue& value, int& out)
{
    if (value.type() != MpackValue::Int && value.type() != MpackValue::UInt)
        return false;
    try
    {
        const int64_t raw = value.as_int();
        if (raw < static_cast<int64_t>(std::numeric_limits<int>::min())
            || raw > static_cast<int64_t>(std::numeric_limits<int>::max()))
            return false;
        out = static_cast<int>(raw);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

enum class RedrawEventType
{
    BusyStart,
    BusyStop,
    DefaultColorsSet,
    Flush,
    GridClear,
    GridCursorGoto,
    GridLine,
    GridResize,
    GridScroll,
    HlAttrDefine,
    ModeChange,
    ModeInfoSet,
    OptionSet,
    SetTitle,
};

struct RedrawDispatchEntry
{
    std::string_view name;
    RedrawEventType type;
};

constexpr std::array<RedrawDispatchEntry, 14> kRedrawDispatch = { {
    { "busy_start", RedrawEventType::BusyStart },
    { "busy_stop", RedrawEventType::BusyStop },
    { "default_colors_set", RedrawEventType::DefaultColorsSet },
    { "flush", RedrawEventType::Flush },
    { "grid_clear", RedrawEventType::GridClear },
    { "grid_cursor_goto", RedrawEventType::GridCursorGoto },
    { "grid_line", RedrawEventType::GridLine },
    { "grid_resize", RedrawEventType::GridResize },
    { "grid_scroll", RedrawEventType::GridScroll },
    { "hl_attr_define", RedrawEventType::HlAttrDefine },
    { "mode_change", RedrawEventType::ModeChange },
    { "mode_info_set", RedrawEventType::ModeInfoSet },
    { "option_set", RedrawEventType::OptionSet },
    { "set_title", RedrawEventType::SetTitle },
} };

// Entries must be in alphabetical order — binary search requires sorted input.
static_assert(
    []() constexpr {
        for (size_t i = 1; i < kRedrawDispatch.size(); ++i)
            if (kRedrawDispatch[i].name < kRedrawDispatch[i - 1].name)
                return false;
        return true;
    }(),
    "kRedrawDispatch must be sorted alphabetically for binary search to be correct");

const RedrawDispatchEntry* find_redraw_dispatch(std::string_view name)
{
    const auto it = std::lower_bound(kRedrawDispatch.begin(), kRedrawDispatch.end(), name,
        [](const RedrawDispatchEntry& entry, std::string_view value) { return entry.name < value; });
    if (it == kRedrawDispatch.end() || it->name != name)
        return nullptr;
    return std::to_address(it);
}

} // namespace

void UiEventHandler::process_redraw(const std::vector<MpackValue>& params)
{
    PERF_MEASURE();
    auto dispatch_batch = [&](RedrawEventType type, const MpackValue& args) {
        switch (type)
        {
        case RedrawEventType::GridLine:
            handle_grid_line(args);
            break;
        case RedrawEventType::GridCursorGoto:
            handle_grid_cursor_goto(args);
            break;
        case RedrawEventType::GridScroll:
            handle_grid_scroll(args);
            break;
        case RedrawEventType::GridClear:
            handle_grid_clear(args);
            break;
        case RedrawEventType::GridResize:
            handle_grid_resize(args);
            break;
        case RedrawEventType::HlAttrDefine:
            handle_hl_attr_define(args);
            break;
        case RedrawEventType::DefaultColorsSet:
            handle_default_colors_set(args);
            break;
        case RedrawEventType::ModeInfoSet:
            handle_mode_info_set(args);
            break;
        case RedrawEventType::ModeChange:
            handle_mode_change(args);
            break;
        case RedrawEventType::OptionSet:
            handle_option_set(args);
            break;
        case RedrawEventType::SetTitle:
            handle_set_title(args);
            break;
        case RedrawEventType::Flush:
        case RedrawEventType::BusyStart:
        case RedrawEventType::BusyStop:
            break;
        }
    };

    for (size_t ei = 0; ei < params.size(); ei++)
    {
        auto& event = params[ei];
        const auto* event_array = try_get_array(event);
        if (!event_array || event_array->empty())
            continue;

        const auto* name = try_get_string((*event_array)[0]);
        if (!name)
            continue;

        const auto* dispatch = find_redraw_dispatch(*name);
        if (!dispatch)
            continue;

        if (dispatch->type == RedrawEventType::Flush)
        {
            invoke_callback("on_flush", on_flush);
            continue;
        }

        if (dispatch->type == RedrawEventType::BusyStart)
        {
            invoke_callback("on_busy", on_busy, true);
            continue;
        }

        if (dispatch->type == RedrawEventType::BusyStop)
        {
            invoke_callback("on_busy", on_busy, false);
            continue;
        }

        for (size_t i = 1; i < event_array->size(); i++)
        {
            const auto& args = (*event_array)[i];
            dispatch_batch(dispatch->type, args);
        }
    }
}

void UiEventHandler::handle_grid_line(const MpackValue& args)
{
    PERF_MEASURE();
    if (!grid_)
        return;

    const auto* args_array = try_get_array(args);
    if (!args_array || args_array->size() < 4)
        return;

    int row = 0;
    int col_start = 0;
    if (!try_get_int((*args_array)[1], row) || !try_get_int((*args_array)[2], col_start))
        return;

    const int grid_rows = grid_->sink_rows();
    const int grid_cols = grid_->sink_cols();
    const bool rows_known = grid_rows > 0;
    const bool cols_known = grid_cols > 0;
    if (row < 0 || (rows_known && row >= grid_rows) || col_start < 0 || (cols_known && col_start >= grid_cols))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "handle_grid_line: out-of-range coordinates row=%d col_start=%d grid=%dx%d — dropping event",
            row, col_start, grid_cols, grid_rows);
        return;
    }

    int col = col_start;
    uint16_t current_hl = 0;
    bool truncated = false;

    const auto* cells = try_get_array((*args_array)[3]);
    if (!cells)
        return;

    for (const auto& cell : *cells)
    {
        const auto* cell_array = try_get_array(cell);
        if (!cell_array || cell_array->empty())
            continue;

        const auto* text = try_get_string((*cell_array)[0]);
        if (!text)
            continue;

        if (cell_array->size() >= 2 && !(*cell_array)[1].is_nil())
        {
            int hl_id = 0;
            if (!try_get_int((*cell_array)[1], hl_id))
                continue;
            if (hl_id < 0 || hl_id > static_cast<int>(kMaxAttrId))
            {
                DRAXUL_LOG_WARN(LogCategory::App, "handle_grid_line: attr_id %d out of range; clamping to 0", hl_id);
                hl_id = 0;
            }
            current_hl = static_cast<uint16_t>(hl_id);
        }

        int repeat = 1;
        if (cell_array->size() >= 3 && (!try_get_int((*cell_array)[2], repeat) || repeat < 1))
            continue;
        // Clamp repeat to remaining columns to prevent main-thread stalls
        // from malformed packets with excessively large repeat values.
        if (cols_known && repeat > grid_cols - col)
            repeat = std::max(0, grid_cols - col);

        bool dw = cluster_cell_width(*text, options_ ? *options_ : UiOptions{}) == 2;
        for (int r = 0; r < repeat; r++)
        {
            if (cols_known && col >= grid_cols)
            {
                truncated = true;
                break;
            }
            grid_->set_cell(col, row, *text, current_hl, dw);
            col++;
            if (dw)
                col++;
        }
        if (truncated)
            break;
    }

    if (truncated)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "handle_grid_line: column overflow at row=%d col=%d grid_cols=%d — truncated",
            row, col, grid_cols);
    }
}

void UiEventHandler::handle_grid_cursor_goto(const MpackValue& args)
{
    if (args.type() != MpackValue::Array || args.as_array().size() < 3)
        return;
    const auto& args_array = args.as_array();
    int row = 0, col = 0;
    if (!try_get_int(args_array[1], row) || !try_get_int(args_array[2], col))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_grid_cursor_goto: expected integer args — skipping");
        return;
    }
    cursor_row_ = row;
    cursor_col_ = col;
    invoke_callback("on_cursor_goto", on_cursor_goto, cursor_col_, cursor_row_);
}

void UiEventHandler::handle_grid_scroll(const MpackValue& args)
{
    if (!grid_ || args.type() != MpackValue::Array || args.as_array().size() < 7)
        return;
    const auto& args_array = args.as_array();
    int top = 0, bot = 0, left = 0, right = 0, rows = 0, cols = 0;
    if (!try_get_int(args_array[1], top) || !try_get_int(args_array[2], bot) || !try_get_int(args_array[3], left)
        || !try_get_int(args_array[4], right) || !try_get_int(args_array[5], rows))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_grid_scroll: expected integer args — skipping");
        return;
    }
    if (args_array.size() >= 7)
        try_get_int(args_array[6], cols);

    grid_->scroll(top, bot, left, right, rows, cols);
}

void UiEventHandler::handle_grid_clear(const MpackValue& /*args*/)
{
    if (!grid_)
        return;
    grid_->clear();
}

void UiEventHandler::handle_grid_resize(const MpackValue& args)
{
    if (args.type() != MpackValue::Array || args.as_array().size() < 3)
        return;
    const auto& args_array = args.as_array();
    int cols = 0, rows = 0;
    if (!try_get_int(args_array[1], cols) || !try_get_int(args_array[2], rows))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_grid_resize: expected integer args — skipping");
        return;
    }
    if (grid_)
        grid_->resize(cols, rows);
    invoke_callback("on_grid_resize", on_grid_resize, cols, rows);
}

void UiEventHandler::handle_hl_attr_define(const MpackValue& args)
{
    if (!highlights_ || args.type() != MpackValue::Array || args.as_array().size() < 2)
        return;
    const auto& args_array = args.as_array();
    int raw_id_int = 0;
    if (!try_get_int(args_array[0], raw_id_int))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_hl_attr_define: expected integer attr_id — skipping");
        return;
    }
    auto raw_id = static_cast<int64_t>(raw_id_int);
    if (raw_id < 0 || raw_id > static_cast<int64_t>(kMaxAttrId))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_hl_attr_define: attr_id %lld out of range; clamping to 0",
            static_cast<long long>(raw_id));
        raw_id = 0;
    }
    auto id = static_cast<uint16_t>(raw_id);
    const auto& attrs = args_array[1];

    HlAttr hl = {};
    if (attrs.type() == MpackValue::Map)
    {
        for (auto& [key, val] : attrs.as_map())
        {
            const auto* k = try_get_string(key);
            if (!k)
                continue;
            if (*k == "foreground")
            {
                int color = 0;
                if (try_get_int(val, color))
                {
                    hl.fg = color_from_rgb(static_cast<uint32_t>(color));
                    hl.has_fg = true;
                }
            }
            else if (*k == "background")
            {
                int color = 0;
                if (try_get_int(val, color))
                {
                    hl.bg = color_from_rgb(static_cast<uint32_t>(color));
                    hl.has_bg = true;
                }
            }
            else if (*k == "special")
            {
                int color = 0;
                if (try_get_int(val, color))
                {
                    hl.sp = color_from_rgb(static_cast<uint32_t>(color));
                    hl.has_sp = true;
                }
            }
            else if (*k == "bold")
            {
                if (val.type() == MpackValue::Bool)
                    hl.bold = val.as_bool();
            }
            else if (*k == "italic")
            {
                if (val.type() == MpackValue::Bool)
                    hl.italic = val.as_bool();
            }
            else if (*k == "underline")
            {
                if (val.type() == MpackValue::Bool)
                    hl.underline = val.as_bool();
            }
            else if (*k == "undercurl")
            {
                if (val.type() == MpackValue::Bool)
                    hl.undercurl = val.as_bool();
            }
            else if (*k == "strikethrough")
            {
                if (val.type() == MpackValue::Bool)
                    hl.strikethrough = val.as_bool();
            }
            else if (*k == "reverse")
            {
                if (val.type() == MpackValue::Bool)
                    hl.reverse = val.as_bool();
            }
        }
    }

    highlights_->set(id, hl);
}

void UiEventHandler::handle_default_colors_set(const MpackValue& args)
{
    if (!highlights_ || args.type() != MpackValue::Array || args.as_array().size() < 3)
        return;
    const auto& args_array = args.as_array();
    int fg = 0, bg = 0, sp = 0;
    if (!try_get_int(args_array[0], fg) || !try_get_int(args_array[1], bg) || !try_get_int(args_array[2], sp))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_default_colors_set: expected integer color args — skipping");
        return;
    }

    highlights_->set_default_fg(color_from_rgb(static_cast<uint32_t>(fg)));
    highlights_->set_default_bg(color_from_rgb(static_cast<uint32_t>(bg)));
    highlights_->set_default_sp(color_from_rgb(static_cast<uint32_t>(sp)));
}

void UiEventHandler::handle_mode_info_set(const MpackValue& args)
{
    if (args.type() != MpackValue::Array || args.as_array().size() < 2)
        return;
    const auto& args_array = args.as_array();
    const auto* modes = try_get_array(args_array[1]);
    if (!modes)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_mode_info_set: expected array of modes — skipping");
        return;
    }
    modes_.clear();
    for (auto& m : *modes)
    {
        ModeInfo info;
        if (m.type() == MpackValue::Map)
        {
            for (auto& [key, val] : m.as_map())
            {
                const auto* k = try_get_string(key);
                if (!k)
                    continue;
                if (*k == "name")
                {
                    const auto* s = try_get_string(val);
                    if (s)
                        info.name = *s;
                }
                else if (*k == "cursor_shape")
                {
                    const auto* shape = try_get_string(val);
                    if (shape)
                    {
                        if (*shape == "block")
                            info.cursor_shape = CursorShape::Block;
                        else if (*shape == "horizontal")
                            info.cursor_shape = CursorShape::Horizontal;
                        else if (*shape == "vertical")
                            info.cursor_shape = CursorShape::Vertical;
                    }
                }
                else if (*k == "cell_percentage")
                    try_get_int(val, info.cell_percentage);
                else if (*k == "attr_id")
                    try_get_int(val, info.attr_id);
                else if (*k == "blinkwait")
                    try_get_int(val, info.blinkwait);
                else if (*k == "blinkon")
                    try_get_int(val, info.blinkon);
                else if (*k == "blinkoff")
                    try_get_int(val, info.blinkoff);
            }
        }
        modes_.push_back(std::move(info));
    }
}

void UiEventHandler::handle_mode_change(const MpackValue& args)
{
    if (args.type() != MpackValue::Array || args.as_array().size() < 2)
        return;
    int mode = 0;
    if (!try_get_int(args.as_array()[1], mode))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_mode_change: expected integer mode index — skipping");
        return;
    }
    current_mode_ = mode;
    invoke_callback("on_mode_change", on_mode_change, current_mode_);
}

void UiEventHandler::handle_option_set(const MpackValue& args) const
{
    if (args.type() != MpackValue::Array || args.as_array().size() < 2)
        return;
    const auto& args_array = args.as_array();
    const auto* name = try_get_string(args_array[0]);
    if (!name)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_option_set: expected string option name — skipping");
        return;
    }
    invoke_callback("on_option_set", on_option_set, *name, args_array[1]);
}

void UiEventHandler::handle_set_title(const MpackValue& args) const
{
    if (args.type() != MpackValue::Array || args.as_array().empty())
        return;

    const auto& args_array = args.as_array();
    const auto* title = try_get_string(args_array[0]);
    if (!title)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "handle_set_title: expected string title — skipping");
        return;
    }
    invoke_callback("on_title", on_title, *title);
}

} // namespace draxul
