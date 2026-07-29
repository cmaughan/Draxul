#pragma once

#include <draxul/remote_terminal_protocol.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace draxul
{

struct RemoteTerminalClientOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string expected_server_epoch;
    std::string method_prefix = "fake";
    std::string terminal_id;
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
};

class RemoteTerminalClient
{
public:
    explicit RemoteTerminalClient(RemoteTerminalClientOptions options);

    bool attach(std::string& error);
    bool poll(bool& changed, std::string& error);
    bool send_input(std::string_view text, std::string& error);
    bool resize(int cols, int rows, std::string& error);
    bool take_control(std::string& error);
    bool disconnect(std::string& error);
    bool restart(std::string& error);
    bool read_scrollback(uint64_t offset_from_live, size_t max_rows,
        RemoteTerminalScrollbackPage& page, std::string& error);

    const RemoteTerminalClientOptions& options() const;
    const RemoteTerminalProjection& projection() const;
    std::optional<std::string> take_clipboard_write();
    std::chrono::microseconds last_attach_latency() const;
    const std::string& last_error_code() const;

private:
    bool request(std::string_view method, nlohmann::json params,
        nlohmann::json& result, std::string& error);
    nlohmann::json client_params() const;
    std::string method(std::string_view operation) const;

    RemoteTerminalClientOptions options_;
    RemoteTerminalProjection projection_;
    std::string last_error_code_;
    std::chrono::microseconds last_attach_latency_{ 0 };
};

} // namespace draxul
