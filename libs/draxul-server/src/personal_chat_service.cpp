#include "personal_chat_service.h"
#include "personal_agent_service.h"
#include "personal_chat_process.h"
#include "personal_collection_lock.h"
#include <draxul/personal_chat_protocol.h>
#include <draxul/runtime_path.h>
#include <draxul/unicode.h>
#include <draxul/filesystem_path_text.h>
#include <draxul/log.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <set>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace draxul
{
using Json = nlohmann::json;
class PersonalChatSession
{
public:
    PersonalChatSession(std::filesystem::path root, std::filesystem::path local,
        PersonalAgentDefinition definition, AgentDefinition profile)
        : root_(std::move(root)), local_(std::move(local)), definition_(std::move(definition)), profile_(std::move(profile))
    {
        snapshot_.agent_id=definition_.id;
        worker_=std::jthread([this](std::stop_token stop){work(stop);});
    }
    ~PersonalChatSession() { worker_.request_stop(); wake_.notify_all(); }
    PersonalChatSnapshot snapshot() const { std::lock_guard lock(mutex_); return snapshot_; }
    void schedule_check(bool due, bool available)
    {
        std::lock_guard lock(mutex_);
        backing_available_=available;
        if(!available) schedule_pending_=false;
        else if(due && snapshot_.state!="error") schedule_pending_=true;
        wake_.notify_all();
    }
    ControlMethodResult command(const Json& envelope)
    {
        auto params=envelope;
        params.erase("client_id");params.erase("connection_token");params.erase("session_id");
        std::lock_guard lock(mutex_);
        const auto action=params.value("action","");
        const auto request_id=params.value("request_id","");
        if(!valid_personal_agent_id(request_id)) return ControlMethodResult::error("invalid_params","Expected request_id.");
        if(auto found=submitted_.find(request_id);found!=submitted_.end())
            return found->second==params ? ControlMethodResult::success({{"accepted",true}})
                : ControlMethodResult::error("conflict","Request ID reused with different content.");
        if(commands_.size()>=8 || submitted_.size()>=2048) return ControlMethodResult::error("busy","Conversation command limit reached.");
        if(action=="send")
        {
            const auto text=params.value("text","");
            if(text.empty() || text.size()>16384 || utf8_validated_prefix_length(text,text.size())!=text.size()) return ControlMethodResult::error("invalid_params","Message must be UTF-8 and at most 16 KiB.");
            if(snapshot_.state!="idle" || std::ranges::any_of(commands_,[](const auto& c){return c.value("action","")=="send";})) return ControlMethodResult::error("busy","Wait for this conversation to become ready.");
            snapshot_.state="working"; snapshot_.error.clear(); ++snapshot_.revision;
        }
        else if(action=="approve" || action=="decline")
        {
            if(snapshot_.state!="approval" || params.value("approval_id","")!=snapshot_.approval_id
                || std::ranges::any_of(commands_,[](const auto& c){return c.value("action","")=="approve" || c.value("action","")=="decline";}))
                return ControlMethodResult::error("changed","Approval is no longer pending.");
            snapshot_.state="working"; ++snapshot_.revision;
        }
        else if(action=="stop")
        {
            if(snapshot_.state!="working" && snapshot_.state!="approval") return ControlMethodResult::error("not_running","No active turn.");
            schedule_pending_=false;
        }
        else return ControlMethodResult::error("invalid_params","Unknown chat action.");
        submitted_[request_id]=params;
        commands_.push_back(params); wake_.notify_all();
        return ControlMethodResult::success({{"accepted",true}});
    }
private:
    void publish()
    {
        // Only the provider worker mutates current_. Readers get immutable copies.
        while(current_.messages.size()>128) current_.messages.erase(current_.messages.begin());
        size_t size=0;
        for(auto& message:current_.messages)
        {
            if(message.text.size()>65536) throw std::runtime_error("Provider message exceeded 64 KiB.");
            size+=message.text.size();
        }
        while(size>196608 && current_.messages.size()>1)
        { size-=current_.messages.front().text.size(); current_.messages.erase(current_.messages.begin()); }
        std::lock_guard lock(mutex_);
        current_.revision=++snapshot_.revision;
        snapshot_=current_;
        if(std::ranges::any_of(commands_,[](const auto& c){return c.value("action","")=="send";})) snapshot_.state="working";
    }
    void save()
    {
        if(!persistence_ready_) return;
        std::filesystem::create_directories(local_.parent_path());
        personal_write_atomic(local_,Json{{"schema",1},{"profile",definition_.profile},{"thread_id",thread_persisted_?thread_id_:std::string{}},
            {"messages",current_.messages},{"interrupted",current_.state!="idle"}}.dump());
    }
    void emit(PersonalChatProcess& process, const Json& message, std::stop_token stop)
    { process.write(message.dump()+"\n",stop); }
    int request(PersonalChatProcess& process,std::string method,Json params,std::stop_token stop)
    {
        const int id=++sequence_;
        pending_[id]=method;
        emit(process,{{"id",id},{"method",method},{"params",std::move(params)}},stop);
        return id;
    }
    void item(const Json& value, bool completed)
    {
        const auto type=value.value("type","");
        if(type!="agentMessage")
        { if(!completed && type!="userMessage" && type!="reasoning") ++current_.tool_count; return; }
        const auto id=value.at("id").get<std::string>();
        const bool final=value.contains("phase") && value["phase"]=="final_answer";
        const bool commentary=value.contains("phase") && value["phase"]=="commentary";
        // With no phase, wait for turn completion before exposing the last agent message.
        if(commentary) { ignored_items_.insert(id); return; }
        phases_[id]=final;
        if(final || completed)
        {
            auto& text=answer_items_[id];
            if(value.contains("text")) text=value.at("text").get<std::string>();
            if(completed) last_answer_=id;
            if(final) update_answer(id,text);
        }
    }
    void update_answer(const std::string& id,const std::string& text)
    {
        if(scheduled_turn_) return;
        auto found=std::ranges::find(current_.messages,id,&PersonalChatMessage::id);
        if(found==current_.messages.end()) current_.messages.push_back({id,"assistant",text});
        else found->text=text;
    }
    void open_thread(PersonalChatProcess& process, std::stop_token stop)
    {
            Json params{{"cwd",path_text(root_/"agents"/definition_.id)},
                {"sandbox","workspace-write"},{"approvalPolicy","on-request"},{"approvalsReviewer","auto_review"},
                {"developerInstructions",personal_bootstrap_prompt(root_,definition_.id)+
                    "\nThis is a quiet personal chat. Return the requested answer concisely. Do not narrate routine tool calls or calculations. Ask when you need input."}};
            if(!model_.empty() || thread_id_.empty()) params["model"]=model_.empty()?"gpt-6.1-sol":model_;
            if(!thread_id_.empty()) params["threadId"]=thread_id_;
            request(process,thread_id_.empty()?"thread/start":"thread/resume",std::move(params),stop);
    }
    void event(PersonalChatProcess& process,const Json& message,std::stop_token stop)
    {
        if(message.contains("method"))
        {
            const auto method=message.at("method").get<std::string>();
            const auto params=message.value("params",Json::object());
            if(message.contains("id"))
            {
                if(method=="item/commandExecution/requestApproval" || method=="item/fileChange/requestApproval"
                    || method=="item/permissions/requestApproval")
                {
                    if(!current_.approval_id.empty()) throw std::runtime_error("Provider requested concurrent approvals.");
                    if(params.value("threadId","")!=thread_id_ || params.value("turnId","")!=turn_id_)
                    {
                        emit(process,{{"id",message["id"]},{"error",{{"code",-32602},{"message","Approval does not belong to the active personal turn."}}}},stop);
                        return;
                    }
                    requested_permissions_=nullptr;
                    if(method=="item/permissions/requestApproval")
                    {
                        requested_permissions_=params.at("permissions");
                        if(!requested_permissions_.is_object()) throw std::runtime_error("Invalid provider permission request.");
                    }
                    approval_request_=message["id"];
                    current_.approval_id=approval_request_.dump();
                    current_.approval_text=params.dump(2);
                    if(!requested_permissions_.is_null()) current_.approval_text="Allow grants only the requested permissions for this turn.\n\n"+current_.approval_text;
                    if(current_.approval_text.size()>65536) throw std::runtime_error("Provider approval exceeds display limit.");
                    current_.state="approval";
                }
                else
                {
                    emit(process,{{"id",message["id"]},{"error",{{"code",-32601},{"message","This personal chat panel cannot handle "+method}}}},stop);
                    current_.error="Provider requested an unsupported interaction: "+method;
                }
                return;
            }
            if(params.contains("threadId") && params["threadId"]!=thread_id_) return;
            if(method=="item/started" || method=="item/completed") item(params.at("item"),method=="item/completed");
            else if(method=="item/agentMessage/delta")
            {
                const auto id=params.at("itemId").get<std::string>();
                if(answer_items_.size()>512) throw std::runtime_error("Too many provider messages in one turn.");
                if(ignored_items_.contains(id)) return;
                const auto delta=params.at("delta").get<std::string>();
                turn_text_bytes_+=delta.size();
                if(turn_text_bytes_>262144) throw std::runtime_error("Provider turn text exceeds 256 KiB.");
                auto& answer=answer_items_[id]; answer+=delta;
                if(answer.size()>65536) throw std::runtime_error("Provider answer exceeds 64 KiB.");
                if(phases_.contains(id) && phases_[id]) update_answer(id,answer);
            }
            else if(method=="turn/started") { turn_id_=params.at("turn").at("id").get<std::string>(); thread_persisted_=true; current_.state="working"; save(); }
            else if(method=="turn/completed")
            {
                if(!last_answer_.empty() && !phases_[last_answer_]) update_answer(last_answer_,answer_items_[last_answer_]);
                const auto& turn=params.at("turn");
                if(turn.value("status","")=="failed") current_.error=turn.value("error",Json::object()).dump();
                if(turn.value("status","")=="interrupted") current_.error="Stopped.";
                if(scheduled_turn_ && turn.value("status","")=="completed")
                {
                    const auto answer=Json::parse(answer_items_[last_answer_],nullptr,false);
                    if(!answer.is_object() || !answer.contains("has_update") || !answer["has_update"].is_boolean()
                        || !answer.contains("message") || !answer["message"].is_string())
                        current_.error="Schedule check returned an invalid result.";
                    else if(answer["has_update"].get<bool>())
                        current_.messages.push_back({last_answer_,"assistant",answer["message"].get<std::string>()});
                }
                scheduled_turn_=false;
                turn_id_.clear(); stop_pending_=false; current_.state="idle"; current_.approval_id.clear(); current_.approval_text.clear(); save();
            }
            else if(method=="error") current_.error=params.value("error",params).dump();
            return;
        }
        if(!message.contains("id") || !message["id"].is_number_integer()) return;
        const int id=message["id"].get<int>();
        const auto found=pending_.find(id); if(found==pending_.end()) return;
        const auto method=found->second; pending_.erase(found);
        if(message.contains("error"))
        {
            const auto error=message["error"].value("message","Provider request failed.");
            if(method=="thread/resume" && error.starts_with("no rollout found for thread id "))
            {
                // The folder is the assistant's durable identity. Retain the
                // display transcript, but replace a provider history that is gone.
                DRAXUL_LOG_INFO(LogCategory::App,"Personal assistant %s is starting a new provider thread because its rollout is unavailable",definition_.id.c_str());
                thread_id_.clear(); thread_persisted_=false; current_.error.clear(); save();
                open_thread(process,stop); return;
            }
            if(method=="turn/start" || method=="turn/interrupt")
            { current_.state="idle"; current_.error=error; turn_id_.clear(); stop_pending_=false; scheduled_turn_=false; save(); return; }
            throw std::runtime_error(error);
        }
        const auto& result=message.at("result");
        if(method=="initialize")
        {
            emit(process,{{"method","initialized"}},stop);
            open_thread(process,stop);
        }
        else if(method=="thread/start" || method=="thread/resume")
        {
            thread_id_=result.at("thread").at("id").get<std::string>(); current_.state="idle"; current_.error.clear(); save();
        }
        else if(method=="turn/start") turn_id_=result.at("turn").at("id").get<std::string>();
    }
    static std::string path_text(const std::filesystem::path& path)
    { auto value=path.u8string(); return {reinterpret_cast<const char*>(value.data()),value.size()}; }
    void work(std::stop_token stop)
    {
        current_.agent_id=definition_.id;
        std::string diagnostics;
        try
        {
            CollectionLock executor_lock;
            auto lock_path=local_; lock_path+=".lock";
            executor_lock.acquire(lock_path);
#ifndef _WIN32
            std::filesystem::permissions(local_.parent_path(),std::filesystem::perms::owner_all);
#endif
            if(std::filesystem::exists(local_))
            {
                const auto saved=Json::parse(personal_read_bounded(local_.parent_path(),local_,1048576));
                if(saved.at("schema")!=1 || saved.at("profile")!=definition_.profile) throw std::runtime_error("Stored personal chat format/profile does not match.");
                thread_id_=saved.at("thread_id").get<std::string>();
                thread_persisted_=!thread_id_.empty();
                current_.messages=saved.at("messages").get<std::vector<PersonalChatMessage>>();
                if(saved.at("interrupted").get<bool>()) current_.error="Previous turn was interrupted. It has not been resent.";
            }
            persistence_ready_=true;
            publish();
            unsigned failures=0;
            while(!stop.stop_requested())
            {
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock,stop,[this]{return backing_available_;});
                }
                if(stop.stop_requested()) break;
                const auto began=std::chrono::steady_clock::now();
                try { run_provider(stop); break; }
                catch(const std::exception& error)
                {
                    if(stop.stop_requested()) break;
                    DRAXUL_LOG_WARN(LogCategory::App,"Personal assistant %s provider restart: %s",definition_.id.c_str(),error.what());
                    if(std::chrono::steady_clock::now()-began>std::chrono::seconds(30)) failures=0;
                    failures=std::min(failures+1,6u);
                    const bool interrupted=current_.state=="working" || current_.state=="approval";
                    if(interrupted && !scheduled_turn_)
                        current_.messages.push_back({personal_unique_id(),"system","The assistant restarted before finishing this request. Your message is saved; ask it to continue."});
                    current_.state="connecting";
                    current_.error=failures<3 ? "" : "Waiting for Codex to become available: "+std::string(error.what());
                    current_.approval_id.clear(); current_.approval_text.clear();
                    turn_id_.clear(); pending_.clear(); stop_pending_=false; scheduled_turn_=false;
                    {
                        std::lock_guard lock(mutex_);
                        // Commands whose acceptance preceded a process failure
                        // must not become input to another process implicitly.
                        for(const auto& command:commands_)
                            if(command.value("action","")=="send")
                                current_.messages.push_back({command.at("request_id").get<std::string>(),"user",command.at("text").get<std::string>()});
                        commands_.clear();
                    }
                    save(); publish();
                    std::unique_lock lock(mutex_);
                    wake_.wait_for(lock,stop,std::chrono::seconds(std::min(30u,1u<<(failures-1))),[]{return false;});
                }
            }
            save();
        }
        catch(const std::exception& error)
        {
            current_.state="error"; current_.error=error.what(); current_.approval_id.clear(); current_.approval_text.clear();
            try { if(persistence_ready_ && !thread_id_.empty()) save(); } catch(...) {}
            try { publish(); } catch(...) { std::lock_guard lock(mutex_); snapshot_.state="error"; snapshot_.error=error.what(); }
        }
    }
    void run_provider(std::stop_token stop)
    {
        std::string diagnostics;
            PersonalChatProcess process;
            std::vector<std::string> args{"app-server","--listen","stdio://"};
            for(size_t i=0;i<profile_.default_args.size();++i)
            {
                const auto& arg=profile_.default_args[i];
                if(arg=="--model" || arg=="-m")
                { if(++i==profile_.default_args.size()) throw std::runtime_error("Missing personal profile model."); model_=profile_.default_args[i]; }
                else if(arg.starts_with("--model=")) model_=arg.substr(8);
                else if(arg=="-c" || arg=="--config" || arg=="--enable" || arg=="--disable")
                { args.push_back(arg); if(++i==profile_.default_args.size()) throw std::runtime_error("Missing personal profile option value."); args.push_back(profile_.default_args[i]); }
                else throw std::runtime_error("Personal chat does not support this terminal profile option: "+arg);
            }
            process.start(profile_.executable,args,root_/"agents"/definition_.id);
            request(process,"initialize",{{"clientInfo",{{"name","draxul-personal-chat"},{"version","1"}}}},stop);
            auto started=std::chrono::steady_clock::now();
            std::string input;
            while(!stop.stop_requested())
            {
                std::deque<Json> commands;
                {
                    std::lock_guard lock(mutex_); commands.swap(commands_);
                    if(commands.empty() && schedule_pending_ && current_.state=="idle")
                    {
                        schedule_pending_=false;
                        commands.push_back({{"action","schedule"}});
                        snapshot_.state="working";
                    }
                }
                for(const auto& command:commands)
                {
                    const auto action=command.at("action").get<std::string>();
                    if(action=="send" || action=="schedule")
                    {
                        current_.state="working"; current_.error.clear(); current_.tool_count=0;
                        phases_.clear(); answer_items_.clear(); ignored_items_.clear(); turn_text_bytes_=0; last_answer_.clear();
                        scheduled_turn_=action=="schedule";
                        std::string text;
                        Json params{{"threadId",thread_id_}};
                        if(scheduled_turn_)
                        {
                            const auto timestamp=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                            std::tm utc{};
#ifdef _WIN32
                            gmtime_s(&utc,&timestamp);
#else
                            gmtime_r(&timestamp,&utc);
#endif
                            std::ostringstream formatted; formatted<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%SZ");
                            current_.last_schedule_check=formatted.str(); ++current_.schedule_checks;
                            text="Draxul background schedule check. The server-owned five-minute timer is now active. Current UTC time: "+formatted.str()+
                                ". Read your instructions.md, schedule.md and state.md if present. Check the user's saved schedules "
                                "and timezone, and perform only due work already authorized by the user. Old notes saying no Draxul timer "
                                "exists are superseded by this wakeup. Do not invent tasks. If an external scheduler already owns "
                                "a task, inspect its recorded results instead of duplicating its actions or delivery. "
                                "Use recorded completion times to avoid repeating work. Record progress and the next due time "
                                "in state.md. Do not create another timer or wait loop. Return has_update=false and message='' "
                                "when nothing needs reporting. Otherwise return has_update=true with a concise user-facing result, "
                                "failure or question in message. Do not narrate routine checks or calculations.";
                            params["outputSchema"]={{"type","object"},{"properties",{{"has_update",{{"type","boolean"}}},{"message",{{"type","string"}}}}},
                                {"required",Json::array({"has_update","message"})},{"additionalProperties",false}};
                        }
                        else
                        {
                            text=command.at("text").get<std::string>();
                            current_.messages.push_back({command.at("request_id").get<std::string>(),"user",text});
                        }
                        params["input"]=Json::array({{{"type","text"},{"text",text}}});
                        save();
                        request(process,"turn/start",std::move(params),stop);
                    }
                    else if(action=="stop")
                    {
                        if(!turn_id_.empty()) request(process,"turn/interrupt",{{"threadId",thread_id_},{"turnId",turn_id_}},stop);
                        else stop_pending_=true;
                    }
                    else
                    {
                        Json result;
                        if(!requested_permissions_.is_null())
                            result={{"permissions",action=="approve"?requested_permissions_:Json::object()},{"scope","turn"}};
                        else result={{"decision",action=="approve"?"accept":"decline"}};
                        emit(process,{{"id",approval_request_},{"result",std::move(result)}},stop);
                        requested_permissions_=nullptr;
                        current_.approval_id.clear(); current_.approval_text.clear(); current_.state="working";
                    }
                    publish();
                }
                diagnostics+=process.diagnostics();
                if(diagnostics.size()>4096) diagnostics.erase(0,diagnostics.size()-4096);
                input+=process.read();
                if(input.size()>4*1024*1024) throw std::runtime_error("Provider protocol frame exceeds 4 MiB.");
                bool changed=false;
                for(size_t end;(end=input.find('\n'))!=std::string::npos;)
                {
                    const auto line=input.substr(0,end); input.erase(0,end+1);
                    if(line.empty()) continue;
                    event(process,Json::parse(line),stop); changed=true;
                }
                if(stop_pending_ && !turn_id_.empty())
                { request(process,"turn/interrupt",{{"threadId",thread_id_},{"turnId",turn_id_}},stop); stop_pending_=false; }
                if(changed) publish();
                if(!process.running()) throw std::runtime_error("Codex app-server exited. "+diagnostics);
                if(current_.state=="connecting" && std::chrono::steady_clock::now()-started>std::chrono::seconds(30))
                    throw std::runtime_error("Codex app-server did not become ready within 30 seconds. "+diagnostics);
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock,stop,std::chrono::milliseconds(current_.state=="idle"?250:20),[this]{return !commands_.empty() || (schedule_pending_ && current_.state=="idle");});
            }
    }
    std::filesystem::path root_,local_;
    PersonalAgentDefinition definition_; AgentDefinition profile_;
    mutable std::mutex mutex_; std::condition_variable_any wake_;
    PersonalChatSnapshot snapshot_,current_;
    std::deque<Json> commands_; std::map<std::string,Json> submitted_;
    std::map<int,std::string> pending_; int sequence_=0;
    std::string thread_id_,turn_id_,last_answer_,model_; Json approval_request_,requested_permissions_;
    std::set<std::string> ignored_items_; size_t turn_text_bytes_=0;
    std::map<std::string,bool> phases_; std::map<std::string,std::string> answer_items_;
    bool persistence_ready_=false,stop_pending_=false,scheduled_turn_=false,thread_persisted_=false;
    bool schedule_pending_=false; // Protected by mutex_; at most one deferred check.
    bool backing_available_=true; // Protected by mutex_; pause restarts while Dropbox is unavailable.
    std::jthread worker_;
};
PersonalChatService::PersonalChatService(PersonalAgentService& metadata,std::filesystem::path local_state,std::vector<AgentDefinition> profiles)
    :metadata_(metadata),local_state_(local_state.empty()?user_data_dir()/"draxul"/"personal-chat":std::move(local_state)/"chat")
{ for(auto& profile:profiles) profiles_.register_definition(std::move(profile)); }
PersonalChatService::~PersonalChatService()=default;
void PersonalChatService::erase(std::string_view id){sessions_.erase(std::string(id));}
void PersonalChatService::tick(std::chrono::steady_clock::time_point now)
{
    if(now<next_scan_) return;
    next_scan_=now+std::chrono::seconds(1);
    const bool due=now>=next_wake_;
    if(due) next_wake_=now+std::chrono::minutes(5);
    const auto metadata=metadata_.snapshot();
    // Cancel deferred checks if backing data disappears or deletion has begun.
    for(auto& [id,session]:sessions_)
    {
        const auto found=std::ranges::find(metadata.agents,id,&PersonalAgentDefinition::id);
        const bool available=metadata.error.empty() && found!=metadata.agents.end()
            && found->error.empty() && !metadata_.deleting(id);
        session->schedule_check(due,available);
    }
    if(!metadata.error.empty()) return;
    for(const auto& definition:metadata.agents)
    {
        if(sessions_.contains(definition.id)) continue;
        const auto* profile=profiles_.find(definition.profile);
        if(!profile || profile->kind!="codex" || !definition.error.empty() || metadata_.deleting(definition.id)) continue;
        const auto opened=handle("personal.chat.open",{{"agent_id",definition.id}});
        if(opened.ok) sessions_.at(definition.id)->schedule_check(due,true);
    }
}
std::vector<ServerAgentProjection> PersonalChatService::project(const TopologySnapshot& topology) const
{
    const auto metadata=metadata_.snapshot();
    std::vector<ServerAgentProjection> result;
    for(const auto& space:topology.spaces) for(const auto& tab:space.tabs) for(const auto& pane:tab.panes)
    {
        if(pane.domain!=TopologyPaneDomain::ClientLocal || pane.client_host_kind!="personal-assistant") continue;
        const auto session=sessions_.find(pane.client_source_path);
        const auto definition=std::ranges::find(metadata.agents,pane.client_source_path,&PersonalAgentDefinition::id);
        if(session==sessions_.end() || definition==metadata.agents.end()) continue;
        const auto state=session->second->snapshot();
        const auto* profile=profiles_.find(definition->profile);
        ServerAgentProjection agent;
        agent.space_id=space.space_id; agent.tab_id=tab.tab_id; agent.pane_id=pane.pane_id;
        agent.identity={definition->profile,profile?profile->kind:"codex",definition->name,"personal-"+definition->id,AgentIdentityOrigin::Managed};
        agent.alias=definition->name; agent.identity_evidence_category="personal_chat";
        agent.lifecycle=state.state=="error"?AgentLifecycle::Failed:AgentLifecycle::Running;
        agent.generation={1}; agent.status=state.state=="working"?AgentStatus::Working:
            state.state=="approval"?AgentStatus::Blocked:state.state=="idle"?AgentStatus::Idle:AgentStatus::Unknown;
        agent.status_authority=AgentStateAuthority::DirectHost;
        agent.status_evidence_category="structured_provider";agent.rule_id="personal_chat";
        agent.observation_generation=state.revision;agent.attention=state.state=="approval" || state.state=="error";
        agent.running=state.state!="error"; result.push_back(std::move(agent));
    }
    return result;
}
ControlMethodResult PersonalChatService::handle(std::string_view method,const Json& params)
{
    const auto id=params.value("agent_id","");
    if(!valid_personal_agent_id(id)) return ControlMethodResult::error("invalid_params","Invalid personal agent ID.");
    const auto metadata=metadata_.snapshot();
    const auto found=std::ranges::find(metadata.agents,id,&PersonalAgentDefinition::id);
    if(!metadata.error.empty() || found==metadata.agents.end() || !found->error.empty() || metadata_.deleting(id))
    {
        const auto live=sessions_.find(id);
        if(live!=sessions_.end())
        {
            if(method=="personal.chat.command" && (params.value("action","")=="stop" || params.value("action","")=="decline"))
                return live->second->command(params);
            if(method=="personal.chat.open" || method=="personal.chat.snapshot")
            {
                auto state=live->second->snapshot();state.error="Personal backing data is unavailable. You can still stop this conversation.";
                return ControlMethodResult::success(Json(state));
            }
        }
        return ControlMethodResult::error("unavailable","Personal agent backing data is unavailable.");
    }
    if(method=="personal.chat.open" || method=="personal.chat.reconnect")
    {
        const auto* profile=profiles_.find(found->profile);
        if(!profile || profile->kind!="codex") return ControlMethodResult::error("unsupported_provider","Native personal chat currently supports Codex. This profile needs a structured chat adapter.");
        if(method=="personal.chat.reconnect")
        {
            if(sessions_.contains(id) && sessions_.at(id)->snapshot().state!="error")
                return ControlMethodResult::error("busy","Only a failed conversation can be reconnected.");
            sessions_.erase(id);
        }
        if(!sessions_.contains(id) && sessions_.size()>=kPersonalAgentLimit)
            return ControlMethodResult::error("capacity","Personal conversation limit reached.");
        if(!sessions_.contains(id)) sessions_[id]=std::make_unique<PersonalChatSession>(path_from_utf8(metadata.root),local_state_/metadata.collection_id/(id+".json"),*found,*profile);
    }
    const auto session=sessions_.find(id);
    if(session==sessions_.end()) return ControlMethodResult::error("not_open","Open the personal conversation first.");
    if(method=="personal.chat.command") return session->second->command(params);
    if(method!="personal.chat.open" && method!="personal.chat.snapshot" && method!="personal.chat.reconnect") return ControlMethodResult::error("unknown_method","Unknown chat method.");
    return ControlMethodResult::success(Json(session->second->snapshot()));
}
}
