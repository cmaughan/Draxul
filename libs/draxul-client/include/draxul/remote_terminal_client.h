#pragma once

#include <draxul/client_recovery.h>
#include <draxul/remote_terminal_protocol.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace draxul
{

struct RemoteTerminalClientOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string session_id = "default";
    std::string expected_server_epoch;
    std::string method_prefix = "fake";
    std::string terminal_id;
    std::shared_ptr<ClientRecoveryState> recovery;
    std::optional<std::chrono::milliseconds> request_timeout;
};

class RemoteTerminalProjection
{
public:
    bool attach(const RemoteTerminalAttach& attach, std::string& error);
    bool apply(const RemoteTerminalEvent& event, std::string& error);

    bool attached() const;
    const RemotePaneDescriptor& pane() const;
    const RemoteTerminalVersion& version() const;
    const TerminalSemanticSnapshot& snapshot() const;
    const std::string& controller_client_id() const;
    bool is_controller(std::string_view client_id) const;
    std::optional<std::string> take_clipboard_write();
    std::optional<TerminalDirtySnapshot> take_grid_update();

private:
    bool apply_snapshot(
        const RemoteTerminalEvent& event, bool allow_resync, std::string& error);
    bool apply_delta(
        const TerminalDirtySnapshot& delta, std::string& error);

    bool attached_ = false;
    RemotePaneDescriptor pane_;
    RemoteTerminalVersion version_;
    TerminalSemanticSnapshot snapshot_;
    std::string controller_client_id_;
    std::optional<std::string> pending_clipboard_write_;
    std::optional<TerminalDirtySnapshot> pending_grid_update_;
};

class RemoteTerminalClient
{
public:
    explicit RemoteTerminalClient(RemoteTerminalClientOptions options);

    bool attach(std::string& error);
    bool suspend(std::string& error, uint64_t request_id = 0);
    bool resume(std::string& error);
    bool poll(bool& changed, std::string& error);
    bool send_input(std::string_view text, std::string& error,
        uint64_t request_id = 0);
    bool resize(int cols, int rows, std::string& error,
        uint64_t request_id = 0);
    bool take_control(std::string& error, uint64_t request_id = 0);
    bool disconnect(std::string& error, uint64_t request_id = 0);
    bool restart(std::string& error, uint64_t request_id = 0);
    bool read_scrollback(uint64_t offset_from_live, size_t max_rows,
        RemoteTerminalScrollbackPage& page, std::string& error);

    // Applies transport-decoded state to the same projection used by the
    // legacy attach/poll methods. Session-level transports use these narrow
    // helpers so projection validation and dirty-state behavior stay shared.
    bool accept_attach(const RemoteTerminalAttach& attach,
        std::string& error,
        std::chrono::microseconds latency = {});
    bool accept_events(std::span<const RemoteTerminalEvent> events,
        bool& changed, std::string& error);

    const RemoteTerminalClientOptions& options() const;
    const RemoteTerminalProjection& projection() const;
    std::optional<std::string> take_clipboard_write();
    std::optional<TerminalDirtySnapshot> take_grid_update();
    std::chrono::microseconds last_attach_latency() const;
    const std::string& last_error_code() const;
    uint64_t skipped_unknown_event_count() const noexcept;

private:
    bool request(std::string_view method, nlohmann::json params,
        nlohmann::json& result, std::string& error);
    nlohmann::json client_params() const;
    std::string method(std::string_view operation) const;

    RemoteTerminalClientOptions options_;
    RemoteTerminalProjection projection_;
    std::string last_error_code_;
    uint64_t skipped_unknown_events_ = 0;
    std::chrono::microseconds last_attach_latency_{ 0 };
};

} // namespace draxul
