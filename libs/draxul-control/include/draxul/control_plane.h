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
inline constexpr size_t kControlMaxMessageBytes = 256 * 1024;
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
        std::string* error = nullptr);
    void stop();
    bool running() const;
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
