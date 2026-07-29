#pragma once

#include <draxul/grid_host_base.h>
#include <draxul/mouse_reporter.h>
#include <draxul/selection_manager.h>
#include <draxul/terminal_snapshot.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace draxul
{

struct RemoteTerminalHostOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string session_id = "default";
    std::string server_epoch;
    std::string method_prefix = "fake";
    std::string terminal_id;
};

class RemoteTerminalHost final : public GridHostBase
{
public:
    explicit RemoteTerminalHost(RemoteTerminalHostOptions options);
    ~RemoteTerminalHost() override;

    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    std::string init_error_code() const override;
    void pump() override;
    void on_config_reloaded(const HostReloadConfig& config) override;
    void on_focus_gained() override;
    void on_focus_lost() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    std::optional<MouseCursor> mouse_cursor_at(int px, int py) const override;
    void set_scroll_offset(float px) override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    std::string current_working_directory() const override;
    std::optional<std::chrono::steady_clock::time_point>
    next_deadline() const override;

protected:
    bool initialize_host() override;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override;
    std::string_view host_name() const override;

private:
    struct GridPos
    {
        int col = 0;
        int row = 0;
    };

    GridPos pixel_to_cell(int px, int py) const;
    bool open_link_at(const GridPos& pos, ModifierFlags mod);
    bool copy_active_selection_to_clipboard();
    void send_remote_input(std::string text);
    void send_paste(std::string_view text);
    void apply_mouse_modes(const TerminalMouseModeSnapshot& modes);

    class Impl;
    MouseReporter mouse_reporter_;
    SelectionManager selection_;
    std::unique_ptr<Impl> impl_;
    std::string init_error_;
    std::string last_error_;
    std::string controller_client_id_;
    std::string pending_paste_;
    TerminalSnapshotMetadata metadata_;
    TerminalMouseModeSnapshot mouse_modes_;
    std::optional<GridPos> pending_selection_copy_click_;
    uint64_t scroll_offset_ = 0;
    uint64_t scrollback_total_ = 0;
    std::chrono::microseconds attach_latency_{ 0 };
    int desired_cols_ = 0;
    int desired_rows_ = 0;
};

} // namespace draxul
