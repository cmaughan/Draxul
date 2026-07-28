#pragma once

#include <draxul/grid_host_base.h>
#include <draxul/terminal_snapshot.h>

#include <filesystem>
#include <memory>
#include <string>

namespace draxul
{

struct RemoteTerminalHostOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string server_epoch;
};

class RemoteTerminalHost final : public GridHostBase
{
public:
    explicit RemoteTerminalHost(RemoteTerminalHostOptions options);
    ~RemoteTerminalHost() override;

    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    void pump() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
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
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::string init_error_;
    std::string last_error_;
    std::string controller_client_id_;
    TerminalSnapshotMetadata metadata_;
    int desired_cols_ = 0;
    int desired_rows_ = 0;
};

} // namespace draxul
