#include <draxul/kanban/kanban_host.h>

#include <draxul/host_registry.h>
#include <draxul/kanban/kanban_layout.h>
#include <draxul/kanban/kanban_store.h>
#include <draxul/unicode.h>

#include <algorithm>
#include <chrono>
#include <utility>

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
    HlMuted = 10,
    HlStatus = 11,
};

HlAttr attr(Color fg, Color bg, bool bold = false)
{
    HlAttr result;
    result.fg = fg;
    result.bg = bg;
    result.has_fg = true;
    result.has_bg = true;
    result.bold = bold;
    return result;
}

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
    default:
        return HlCard;
    }
}

bool is_indic_virama(uint32_t cp)
{
    switch (cp)
    {
    case 0x094D:
    case 0x09CD:
    case 0x0A4D:
    case 0x0ACD:
    case 0x0B4D:
    case 0x0BCD:
    case 0x0C4D:
    case 0x0CCD:
    case 0x0D4D:
    case 0x0DCA:
        return true;
    default:
        return false;
    }
}

size_t next_cluster_end(std::string_view text, size_t offset)
{
    uint32_t base = 0;
    if (!draxul::utf8_decode_next(text, offset, base))
        return offset;

    bool consumed_regional_pair = false;
    bool previous_was_virama = false;
    while (offset < text.size())
    {
        const size_t next_start = offset;
        uint32_t cp = 0;
        if (!draxul::utf8_decode_next(text, offset, cp))
            return offset;

        if (cp == 0x200D && offset < text.size())
        {
            uint32_t joined = 0;
            draxul::utf8_decode_next(text, offset, joined);
            previous_was_virama = false;
            continue;
        }

        if (previous_was_virama)
        {
            previous_was_virama = false;
            continue;
        }

        if (is_indic_virama(cp))
        {
            previous_was_virama = true;
            continue;
        }

        if (draxul::is_width_ignorable(cp) || draxul::is_emoji_modifier(cp))
            continue;

        if (!consumed_regional_pair && draxul::is_regional_indicator(base) && draxul::is_regional_indicator(cp))
        {
            consumed_regional_pair = true;
            continue;
        }

        return next_start;
    }

    return offset;
}

int text_cell_width(std::string_view text)
{
    int width = 0;
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t cluster_start = offset;
        offset = next_cluster_end(text, offset);
        width += std::max(1, draxul::cluster_cell_width(text.substr(cluster_start, offset - cluster_start)));
    }
    return width;
}

int max_scroll_row(const KanbanBoard& board, int grid_rows)
{
    const auto layout = layout_kanban_board(board, KanbanSelection{}, KanbanLayoutOptions{
                                                                  .grid_cols = 1,
                                                                  .grid_rows = grid_rows,
                                                                  .scroll_row = 0,
                                                              });
    return std::max(0, layout.content_rows - layout.visible_card_rows);
}

} // namespace

bool KanbanHost::initialize_host()
{
    configure_highlights();
    if (!reload_board())
        return false;

    running_ = true;
    set_content_ready(false);
    suppress_cursor_until(std::chrono::steady_clock::now() + std::chrono::hours(24));
    callbacks().set_window_title(root_.filename().empty() ? "Kanban" : root_.filename().string());
    callbacks().request_frame();
    return true;
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

void KanbanHost::pump()
{
    if (!running_)
        return;

    suppress_cursor_until(std::chrono::steady_clock::now() + std::chrono::hours(24));
    if (redraw_needed_)
        redraw_board();
    advance_cursor_blink(std::chrono::steady_clock::now());
}

void KanbanHost::on_focus_gained()
{
    GridHostBase::on_focus_gained();
    suppress_cursor_until(std::chrono::steady_clock::now() + std::chrono::hours(24));
}

void KanbanHost::on_key(const KeyEvent& event)
{
    apply_navigation_command(navigation_.on_key(event));
}

bool KanbanHost::dispatch_action(std::string_view action)
{
    if (action == "reload")
        return reload_board();
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
    keep_selection_visible();
    clear_before_redraw_ = true;
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
    const Color bg = color_from_rgb(0x101318);
    const Color fg = color_from_rgb(0xD8DEE9);
    highlights().set_default_bg(bg);
    highlights().set_default_fg(fg);
    highlights().set(HlNormal, attr(fg, bg));
    highlights().set(HlHeader, attr(color_from_rgb(0xE8EEF7), color_from_rgb(0x27313C), true));
    highlights().set(HlHeaderActive, attr(color_from_rgb(0xFFF6D6), color_from_rgb(0x365B61), true));
    highlights().set(HlCard, attr(color_from_rgb(0xD8DEE9), bg));
    highlights().set(HlSelected, attr(color_from_rgb(0xFFFFFF), color_from_rgb(0x3E5167), true));
    highlights().set(HlBorder, attr(color_from_rgb(0x56616F), bg));
    highlights().set(HlBug, attr(color_from_rgb(0xFF7A7A), bg, true));
    highlights().set(HlFeature, attr(color_from_rgb(0xF2D272), bg, true));
    highlights().set(HlRefactor, attr(color_from_rgb(0x7DD3A8), bg, true));
    highlights().set(HlMuted, attr(color_from_rgb(0x87909C), bg));
    highlights().set(HlStatus, attr(color_from_rgb(0xB8C0CC), color_from_rgb(0x1B222C)));
}

bool KanbanHost::reload_board()
{
    std::string error;
    const auto root = resolve_kanban_root(launch_options().source_path, launch_options().working_dir, &error);
    if (!error.empty())
    {
        init_error_ = error;
        return false;
    }

    KanbanBoard loaded = load_kanban_board(root, &error);
    if (!error.empty())
    {
        init_error_ = error;
        return false;
    }

    root_ = root;
    board_ = std::move(loaded);
    clamp_selection(board_, selection_);
    keep_selection_visible();
    update_status();
    clear_before_redraw_ = true;
    redraw_needed_ = true;
    callbacks().request_frame();
    return true;
}

void KanbanHost::redraw_board()
{
    if (clear_before_redraw_)
    {
        grid().clear();
        clear_before_redraw_ = false;
    }
    const int cols = grid_cols();
    const int rows = grid_rows();
    if (cols <= 0 || rows <= 0)
        return;

    const KanbanLayout layout = layout_kanban_board(board_, selection_, KanbanLayoutOptions{
                                                                        .grid_cols = cols,
                                                                        .grid_rows = rows,
                                                                        .scroll_row = scroll_row_,
                                                                    });

    const int status_row = std::max(0, rows - 1);
    for (const auto& column_layout : layout.columns)
    {
        if (column_layout.width <= 0)
            continue;

        const auto& column = board_.columns[static_cast<size_t>(column_layout.index)];
        const bool active = column_layout.index == selection_.column;
        const uint16_t header_hl = active ? HlHeaderActive : HlHeader;
        fill_row(0, column_layout.x, column_layout.width, header_hl);
        fill_row(1, column_layout.x, column_layout.width, HlBorder);

        std::string header = column.name + " (" + std::to_string(column.cards.size()) + ")";
        draw_text(column_layout.x + 1, 0, truncate_to_cells(header, column_layout.width - 2), header_hl,
            column_layout.width - 2);
        for (int col = column_layout.x; col < column_layout.x + column_layout.width; ++col)
            set_cell_if_changed(col, 1, "-", HlBorder, false);
        if (column_layout.x > 0)
        {
            for (int row = 0; row < status_row; ++row)
                set_cell_if_changed(column_layout.x, row, "|", HlBorder, false);
        }

        if (column.cards.empty() && layout.visible_card_rows > 0 && rows > 3)
            draw_text(column_layout.x + 1, 3, "(empty)", HlMuted, column_layout.width - 2);
    }

    for (const auto& row : layout.rows)
    {
        const auto& card = board_.columns[static_cast<size_t>(row.column)].cards[static_cast<size_t>(row.card)];
        const uint16_t card_hl = row.selected ? HlSelected : HlCard;
        fill_row(row.y, row.x, row.width, card_hl);

        const std::string icon = icon_for_kind(card.kind);
        const int icon_cells = text_cell_width(icon);
        draw_text(row.x, row.y, icon, row.selected ? HlSelected : icon_highlight(card.kind), row.width);
        const int text_x = row.x + icon_cells + 1;
        const int text_width = row.width - icon_cells - 1;
        draw_text(text_x, row.y, truncate_to_cells(card.file_name, text_width), card_hl, text_width);
    }

    fill_row(status_row, 0, cols, HlStatus);
    std::string status = status_;
    if (layout.content_rows > layout.visible_card_rows && layout.visible_card_rows > 0)
    {
        status += " | rows ";
        status += std::to_string(scroll_row_ + 1);
        status += "-";
        status += std::to_string(std::min(layout.content_rows, scroll_row_ + layout.visible_card_rows));
        status += "/";
        status += std::to_string(layout.content_rows);
    }
    draw_text(0, status_row, truncate_to_cells(status, cols), HlStatus, cols);

    set_cursor_display_override(std::pair<int, int>{ 0, 0 });
    flush_grid();
    redraw_needed_ = false;
}

void KanbanHost::update_status()
{
    status_ = "kanban";
    if (!root_.empty())
        status_ += " | " + root_.string();
    if (const KanbanCard* card = selected_card(board_, selection_))
        status_ += " | " + card->file_name;
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
    if (board_.columns.empty())
        return;

    const KanbanSelection before = selection_;
    selection_.column = std::clamp(
        selection_.column + column_delta,
        0,
        static_cast<int>(board_.columns.size()) - 1);
    const auto& cards = board_.columns[static_cast<size_t>(selection_.column)].cards;
    const int max_card = std::max(0, static_cast<int>(cards.size()) - 1);
    selection_.card = cards.empty() ? 0 : std::clamp(selection_.card + card_delta, 0, max_card);
    clamp_selection(board_, selection_);
    if (selection_.column == before.column && selection_.card == before.card)
        return;

    const int before_scroll = scroll_row_;
    keep_selection_visible();
    if (scroll_row_ != before_scroll)
        clear_before_redraw_ = true;
    update_status();
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
        if (target_column < 0 || target_column >= static_cast<int>(board_.columns.size()))
            return;

        if (!move_card_to_column(board_, selection_, target_column, &error))
        {
            notify_error(error.empty() ? "Failed to move kanban card." : error);
            return;
        }
        selection_.column = target_column;
        changed = true;
    }
    else if (row_delta != 0)
    {
        const auto& cards = board_.columns[static_cast<size_t>(selection_.column)].cards;
        const int target_card = std::clamp(selection_.card + row_delta, 0, static_cast<int>(cards.size()) - 1);
        if (target_card == selection_.card)
            return;

        if (!reorder_card(board_, selection_, row_delta, &error))
        {
            notify_error(error.empty() ? "Failed to reorder kanban card." : error);
            return;
        }
        selection_.card = target_card;
        changed = true;
    }

    if (!changed)
        return;

    clamp_selection(board_, selection_);
    if (!save_kanban_order(board_, &error))
        notify_error(error.empty() ? "Failed to save kanban order." : error);

    keep_selection_visible();
    update_status();
    clear_before_redraw_ = true;
    redraw_needed_ = true;
    callbacks().request_frame();
}

void KanbanHost::open_selected_card()
{
    const KanbanCard* card = selected_card(board_, selection_);
    if (!card)
        return;

    if (!callbacks().open_markdown_source(card->path.string()))
        notify_error("Failed to open Markdown card: " + card->path.string());
}

void KanbanHost::keep_selection_visible()
{
    scroll_row_ = next_scroll_row_for_selection(board_, selection_, scroll_row_, grid_rows());
    scroll_row_ = std::clamp(scroll_row_, 0, max_scroll_row(board_, grid_rows()));
}

void KanbanHost::draw_text(int col, int row, std::string_view text, uint16_t hl, int max_cells)
{
    if (row < 0 || row >= grid_rows() || col >= grid_cols() || max_cells <= 0)
        return;

    int used = 0;
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t cluster_start = offset;
        offset = next_cluster_end(text, offset);
        const std::string_view cluster = text.substr(cluster_start, offset - cluster_start);
        const int cluster_cells = std::max(1, draxul::cluster_cell_width(cluster));
        if (used + cluster_cells > max_cells || col + used >= grid_cols())
            break;

        set_cell_if_changed(col + used, row, cluster, hl, cluster_cells == 2);
        used += cluster_cells;
    }
}

void KanbanHost::fill_row(int row, int col, int width, uint16_t hl)
{
    if (row < 0 || row >= grid_rows() || width <= 0)
        return;

    const int begin = std::max(0, col);
    const int end = std::min(grid_cols(), col + width);
    for (int x = begin; x < end; ++x)
        set_cell_if_changed(x, row, " ", hl, false);
}

void KanbanHost::set_cell_if_changed(int col, int row, std::string_view text, uint16_t hl, bool double_width)
{
    if (col < 0 || col >= grid_cols() || row < 0 || row >= grid_rows())
        return;

    const auto& current = grid().get_cell(col, row);
    if (!current.double_width_cont
        && current.text.view() == text
        && current.hl_attr_id == hl
        && current.double_width == double_width)
    {
        return;
    }

    grid().set_cell(col, row, std::string(text), hl, double_width);
}

void KanbanHost::notify_error(std::string_view message)
{
    init_error_ = std::string(message);
    callbacks().push_toast(2, message);
}

std::unique_ptr<draxul::IHost> create_kanban_host()
{
    return std::make_unique<KanbanHost>();
}

void register_kanban_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::Kanban, create_kanban_host);
}

} // namespace draxul::kanban
