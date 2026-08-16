#pragma once

#include <draxul/terminal_snapshot.h>
#include <draxul/terminal_surface_host_base.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace draxul
{

class ClientRecoveryState;

struct RemoteTerminalHostOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string session_id = "default";
    std::string server_epoch;
    std::string method_prefix = "fake";
    std::string terminal_id;
    std::shared_ptr<ClientRecoveryState> recovery;
    bool presentation_suspend_supported = false;
};

class RemoteTerminalHost final : public TerminalSurfaceHostBase
{
public:
    explicit RemoteTerminalHost(RemoteTerminalHostOptions options);
    ~RemoteTerminalHost() override;

    void shutdown() override;
    bool is_running() const override;
    std::optional<int> exit_code() const override;
    std::string init_error() const override;
    std::string init_error_code() const override;
    void pump() override;
    void set_presentation_visible(bool visible) override;
    bool requires_periodic_wake() const override;
    void on_config_reloaded(const HostReloadConfig& config) override;
    void on_focus_gained() override;
    void on_focus_lost() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string display_name() const override;
    std::string status_text() const override;
    std::string current_working_directory() const override;
    std::optional<std::chrono::steady_clock::time_point>
    next_deadline() const override;

protected:
    bool initialize_host() override;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override;
    std::string_view host_name() const override;
    bool surface_mouse_reporting_allowed() const override;
    void surface_write_mouse_report(std::string_view sequence) override;
    void surface_scroll_lines(int rows) override;
    glm::ivec2 surface_cursor_position() const override;

private:
    bool handle_scrollback_key(const KeyEvent& event);
    void scroll_to_live();
    void show_observer_input_hint();
    void send_remote_input(std::string text);
    void send_paste(std::string_view text);
    void apply_mouse_modes(const TerminalMouseModeSnapshot& modes);

    class Impl;
    std::shared_ptr<Impl> impl_;
    std::string init_error_;
    std::string last_error_;
    std::string controller_client_id_;
    std::string remote_display_name_;
    bool remote_process_running_ = true;
    std::optional<int> remote_exit_code_;
    std::string pending_paste_;
    TerminalSnapshotMetadata metadata_;
    TerminalMouseModeSnapshot mouse_modes_;
    bool observer_input_hint_shown_ = false;
    uint64_t scroll_offset_ = 0;
    uint64_t scrollback_total_ = 0;
    std::chrono::microseconds attach_latency_{ 0 };
    int desired_cols_ = 0;
    int desired_rows_ = 0;
    std::string published_window_title_;
    std::unordered_map<HlAttr, uint16_t, HlAttrHash>
        remote_attr_ids_;
    uint32_t next_remote_attr_id_ = 1;
};

} // namespace draxul
