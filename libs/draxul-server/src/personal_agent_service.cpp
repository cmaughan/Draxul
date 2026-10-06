#include "personal_agent_service.h"
#include <draxul/personal_agent_protocol.h>
#include <draxul/runtime_path.h>
#include <draxul/process_util.h>
#include <draxul/unicode.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <fstream>
#include <sstream>

namespace draxul
{
namespace
{
std::string path_text(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

class CollectionLock
{
public:
    ~CollectionLock()
    {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (fd_ >= 0) { flock(fd_, LOCK_UN); close(fd_); }
#endif
    }
    void acquire(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Another local server owns this collection, or its lock is unavailable.");
#else
        fd_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd_ < 0 || flock(fd_, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("Another local server owns this collection, or its lock is unavailable.");
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

}

PersonalAgentService::PersonalAgentService(std::filesystem::path root,
    std::filesystem::path local_state, std::vector<AgentDefinition> profiles)
    : root_(std::move(root))
    , local_state_(local_state.empty() ? user_data_dir() / "draxul" / "personal-metadata" : std::move(local_state))
    , authority_(personal_unique_id())
{
    for (auto& profile : profiles) profiles_.register_definition(std::move(profile));
    snapshot_.root = path_text(root_);
    snapshot_.authority = authority_;
    worker_ = std::jthread([this](std::stop_token stop) { work(stop); });
}
PersonalAgentService::~PersonalAgentService()
{
    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}
PersonalAgentSnapshot PersonalAgentService::snapshot() const
{
    std::lock_guard lock(mutex_);
    return snapshot_;
}

bool PersonalAgentService::deleting(std::string_view id) const
{
    std::lock_guard lock(mutex_);
    for (const auto& [request,command] : submitted_)
        if (command.value("action","")=="delete" && command.contains("expected")
            && command["expected"].value("id","")==id)
        {
            const auto& result=results_.at(request);
            if (!result.value("done",false)) return true;
        }
    return false;
}

ControlMethodResult PersonalAgentService::handle(std::string_view method, const nlohmann::json& params)
{
    try
    {
        const auto id = params.at("request_id").get<std::string>();
        if (!valid_personal_agent_id(id))
            return ControlMethodResult::error("invalid_params", "Expected a bounded request_id.");
        std::lock_guard lock(mutex_);
        if (method == "personal.result")
        {
            const auto found = results_.find(id);
            return found == results_.end()
                ? ControlMethodResult::error("unknown_request", "Request is not known to this server; do not blindly replay it.")
                : ControlMethodResult::success(found->second);
        }
        if (params.at("authority").get<std::string>() != authority_)
            return ControlMethodResult::error("server_replaced", "Reload the collection before submitting work.");
        auto command = params;
        command.erase("client_id"); command.erase("connection_token"); command.erase("session_id");
        const auto found = submitted_.find(id);
        if (found != submitted_.end())
        {
            if (found->second != command)
                return ControlMethodResult::error("request_conflict", "Request ID was used with different input.");
            return ControlMethodResult::success(results_.at(id));
        }
        if (commands_.size() >= 16 || results_.size() >= 1024 || command.dump().size() > 65536)
            return ControlMethodResult::error("capacity", "Personal command queue or session request limit reached.");
        results_[id] = {{"done", false}, {"request_id", id}};
        submitted_[id] = command;
        commands_.push_back(std::move(command));
        wake_.notify_all();
        return ControlMethodResult::success(results_[id]);
    }
    catch (const std::exception& e) { return ControlMethodResult::error("invalid_params", e.what()); }
}

void PersonalAgentService::refresh()
{
    auto loaded = load_personal_agents(root_, current_);
    loaded.authority = authority_;
    loaded.profiles.clear();
    for (const auto& p : profiles_.definitions())
        if (p.kind == "codex" || p.kind == "claude") loaded.profiles.push_back(p.profile_id);
    current_ = std::move(loaded);
}

nlohmann::json PersonalAgentService::execute(const nlohmann::json& command)
{
    CollectionLock lock;
    refresh();
    if (current_.root.empty() || !current_.error.empty()
        || command.at("collection_id").get<std::string>() != current_.collection_id)
        throw std::runtime_error("Collection is unavailable or changed. Reload before continuing.");
    lock.acquire(local_state_ / (current_.collection_id + ".lock"));
    const auto action = command.at("action").get<std::string>();
    if (action=="delete")
    {
        if (!command.value("confirmed",false)) throw std::runtime_error("Deletion requires confirmation.");
        delete_personal_agent(root_,current_.collection_id,personal_definition_from_json(command.at("expected")));
        refresh();
        return {{"message","Agent deleted. Backing files are in .deleted."}};
    }
    auto candidate = personal_definition_from_json(command.at("definition"));
    std::optional<PersonalAgentDefinition> expected;
    if (action == "create")
    {
        const auto* profile = profiles_.find(candidate.profile);
        if (!profile || (profile->kind != "codex" && profile->kind != "claude"))
            throw std::runtime_error("Choose a Codex or Claude profile.");
        candidate = {.id=personal_unique_id(), .name=profile->display_name, .profile=profile->profile_id,
            .instructions="# Standing instructions\n\nFollow the user's requests in your chat. Keep durable context in this folder.\n"};
    }
    else if (action == "rename")
    {
        expected = personal_definition_from_json(command.at("expected"));
        const auto name = candidate.name;
        candidate = *expected;
        candidate.name = name;
    }
    else throw std::runtime_error("Unknown personal action; use create or rename.");
    auto saved = save_personal_agent(root_,current_.collection_id,std::move(candidate),expected);
    refresh();
    return {{"definition",personal_definition_to_json(saved)}, {"message",action=="create" ? "Agent ready to start." : "Agent renamed."}};
}

void PersonalAgentService::work(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        nlohmann::json command;
        {
            std::lock_guard lock(mutex_);
            if (!commands_.empty()) { command=std::move(commands_.front()); commands_.pop_front(); }
        }
        if (!command.is_null())
        {
            nlohmann::json result;
            try { result={{"done",true},{"ok",true},{"result",execute(command)}}; }
            catch(const std::exception& e) { result={{"done",true},{"ok",false},{"error",std::string(e.what()).substr(0,512)}}; }
            std::lock_guard lock(mutex_);
            snapshot_=current_;
            results_[command.at("request_id").get<std::string>()]=std::move(result);
        }
        refresh();
        std::unique_lock lock(mutex_);
        snapshot_=current_;
        wake_.wait_for(lock,stop,std::chrono::seconds(1),[this]{return !commands_.empty();});
    }
}
}
