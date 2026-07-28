#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace draxul
{

inline constexpr int kControlProtocolVersion = 1;
// Terminal snapshots also travel over this endpoint in the experimental
// server/client path. Individual methods apply tighter semantic limits.
inline constexpr size_t kControlMaxMessageBytes = 8 * 1024 * 1024;
inline constexpr size_t kControlMaxJsonDepth = 32;

struct ControlRequest
{
    std::string id;
    std::string method;
    nlohmann::json params = nlohmann::json::object();
};

struct ControlMethodResult
{
    bool ok = false;
    nlohmann::json value;
    std::string error_code;
    std::string error_message;

    static ControlMethodResult success(nlohmann::json result);
    static ControlMethodResult error(std::string code, std::string message);
};

struct ControlClientResult
{
    bool ok = false;
    nlohmann::json result;
    std::string error_code;
    std::string error_message;
};

std::filesystem::path control_runtime_directory(
    const std::filesystem::path& config_directory);
std::filesystem::path control_metadata_path(
    const std::filesystem::path& runtime_directory, std::string_view session_id);
// Builds a stable per-runtime namespace for global services. This matters on
// Windows, where the named-pipe namespace is not scoped by the metadata
// directory, and keeps test runtime overrides isolated from production.
std::string namespaced_control_id(std::string_view base_id,
    const std::filesystem::path& runtime_directory);

class ControlServer
{
public:
    using Handler = std::function<ControlMethodResult(const ControlRequest&)>;

    ControlServer();
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(std::string session_id,
        std::filesystem::path runtime_directory,
        std::function<void()> wake_main_thread,
        std::string* error = nullptr,
        nlohmann::json metadata_extra = nlohmann::json::object());
    void stop();
    bool running() const;
    // True when the last start() failed specifically because another live
    // process already owns this Session's endpoint — the signal a caller needs
    // to tell "someone else is running this Session" apart from "the endpoint
    // could not be created here". Cleared by a successful start().
    bool endpoint_in_use() const;
    void process_pending(const Handler& handler);

    const std::string& endpoint() const;
    const std::filesystem::path& metadata_path() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class ControlClient
{
public:
    static ControlClientResult request(std::string_view session_id,
        const std::filesystem::path& runtime_directory,
        std::string_view method,
        nlohmann::json params = nlohmann::json::object());
};

} // namespace draxul
