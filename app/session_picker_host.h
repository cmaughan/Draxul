#pragma once

#include "self_launch.h"
#include "session_picker.h"

#include <draxul/host.h>
#include <draxul/renderer.h>

#include <filesystem>
#include <functional>
#include <memory>

namespace draxul
{

class SessionPickerHost : public IHost
{
public:
    using LaunchFunction = std::function<SelfLaunchResult(const SelfLaunchCommand&)>;

    explicit SessionPickerHost(std::filesystem::path executable_path,
        LaunchFunction launch = launch_self_process);

    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    void set_viewport(const HostViewport& viewport) override;
    void pump() override;
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

private:
    bool activate_or_restore_session(std::string_view session_id, std::string* error);
    bool create_new_session(std::string_view session_name, std::string* error);
    bool kill_session(std::string_view session_id, std::string* error);
    bool spawn_draxul(std::vector<std::string> args, std::string* error) const;
    void refresh_grid();
    void flush_atlas_if_dirty();

    std::filesystem::path executable_path_;
    LaunchFunction launch_;
    SessionPicker picker_;
    std::unique_ptr<IGridHandle> handle_;
    IHostCallbacks* callbacks_ = nullptr;
    IGridRenderer* renderer_ = nullptr;
    TextService* text_service_ = nullptr;
    int pixel_x_ = 0;
    int pixel_y_ = 0;
    int pixel_w_ = 0;
    int pixel_h_ = 0;
    bool running_ = true;
};

} // namespace draxul
