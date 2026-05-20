#pragma once

#include <draxul/grid_host_base.h>
#include <draxul/kanban/kanban_board.h>
#include <draxul/kanban/kanban_layout.h>
#include <draxul/kanban/kanban_navigation.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
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
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;
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
    bool reload_board();
    void redraw_board();
    void redraw_selection_change(KanbanSelection previous_selection);
    void draw_column_header(const KanbanColumnLayout& column_layout, int status_row);
    void draw_card_row(const KanbanCardRowLayout& row);
    void draw_status_row(const KanbanLayout& layout);
    void update_status();
    void apply_navigation_command(KanbanNavigationCommand command);
    void update_key_repeat(const draxul::KeyEvent& event, KanbanNavigationCommand command);
    void pump_key_repeat(std::chrono::steady_clock::time_point now);
    void move_selection(int column_delta, int card_delta);
    void move_card(int column_delta, int row_delta);
    void open_selected_card();
    void keep_selection_visible();
    void draw_text(int col, int row, std::string_view text, uint16_t hl, int max_cells);
    void fill_row(int row, int col, int width, uint16_t hl);
    void set_cell_if_changed(int col, int row, std::string_view text, uint16_t hl, bool double_width);
    void notify_error(std::string_view message);

    std::filesystem::path root_;
    KanbanBoard board_;
    KanbanSelection selection_;
    KanbanNavigationState navigation_;
    std::string init_error_;
    std::string status_;
    bool running_ = false;
    bool redraw_needed_ = true;
    bool clear_before_redraw_ = true;
    int scroll_row_ = 0;
    std::optional<KanbanSelection> selection_before_redraw_;
    std::optional<KanbanNavigationCommand> held_selection_command_;
    int held_keycode_ = 0;
    std::chrono::steady_clock::time_point next_repeat_at_{};
};

std::unique_ptr<draxul::IHost> create_kanban_host();
void register_kanban_host_provider(draxul::HostProviderRegistry& registry);

} // namespace draxul::kanban
