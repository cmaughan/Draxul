#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace draxul
{

namespace session_attach_detail
{
class SessionTransport;
}

class SessionAttachServer
{
public:
    struct LiveSessionInfo
    {
        int workspace_count = 0;
        int pane_count = 0;
        bool detached = false;
        uint64_t owner_pid = 0;
        int64_t last_attached_unix_s = 0;
        int64_t last_detached_unix_s = 0;
    };

    enum class Command
    {
        Activate,
        Detach,
        Shutdown,
        QueryLiveSession,
    };

    using CommandHandler = std::function<void(Command)>;
    using QueryHandler = std::function<LiveSessionInfo()>;
    using RenameHandler = std::function<void(std::string_view)>;

    enum class AttachStatus
    {
        Attached,
        NoServer,
        Error,
    };

    enum class ProbeStatus
    {
        Running,
        NoServer,
        Error,
    };

    SessionAttachServer();
    ~SessionAttachServer();

    SessionAttachServer(const SessionAttachServer&) = delete;
    SessionAttachServer& operator=(const SessionAttachServer&) = delete;

    bool start(std::string_view session_id, CommandHandler on_command_requested,
        QueryHandler on_query_requested, RenameHandler on_rename_requested,
        std::string* error = nullptr);
    bool start(std::string_view session_id, CommandHandler on_command_requested,
        QueryHandler on_query_requested, std::string* error = nullptr);
    bool start(std::string_view session_id, CommandHandler on_command_requested,
        std::string* error = nullptr);
    bool start(std::string_view session_id, std::function<void()> on_attach_requested,
        std::string* error = nullptr)
    {
        return start(session_id,
            [callback = std::move(on_attach_requested)](Command command) {
                if (command == Command::Activate && callback)
                    callback();
            },
            error);
    }
    bool start(std::function<void()> on_attach_requested, std::string* error = nullptr)
    {
        return start("default",
            [callback = std::move(on_attach_requested)](Command command) {
                if (command == Command::Activate && callback)
                    callback();
            },
            error);
    }
    void stop();

    bool running() const
    {
        return running_.load();
    }

    static AttachStatus send_command(
        std::string_view session_id, Command command, std::string* error = nullptr);
    static AttachStatus send_command(Command command, std::string* error = nullptr)
    {
        return send_command("default", command, error);
    }
    static AttachStatus try_attach(std::string_view session_id, std::string* error = nullptr);
    static AttachStatus try_attach(std::string* error = nullptr)
    {
        return try_attach("default", error);
    }
    static ProbeStatus probe(std::string_view session_id, std::string* error = nullptr);
    static ProbeStatus probe(std::string* error = nullptr)
    {
        return probe("default", error);
    }
    static bool query_live_session(
        std::string_view session_id, LiveSessionInfo* info, std::string* error = nullptr);
    static bool query_live_session(LiveSessionInfo* info, std::string* error = nullptr)
    {
        return query_live_session("default", info, error);
    }
    static bool rename_session(
        std::string_view session_id, std::string_view session_name, std::string* error = nullptr);
    static bool rename_session(std::string_view session_name, std::string* error = nullptr)
    {
        return rename_session("default", session_name, error);
    }

private:
    std::string session_id_ = "default";
    CommandHandler on_command_requested_;
    QueryHandler on_query_requested_;
    RenameHandler on_rename_requested_;
    std::atomic<bool> stop_requested_ = false;
    std::atomic<bool> running_ = false;
    std::thread server_thread_;

    std::unique_ptr<session_attach_detail::SessionTransport> transport_;
};

} // namespace draxul
