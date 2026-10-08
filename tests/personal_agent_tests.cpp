#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"
#include <draxul/personal_agent_protocol.h>
#include <draxul/personal_agent_client.h>
#include <draxul/personal_agent_store.h>

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

namespace
{
void write_personal_file(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

void create_collection(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root / "agents" / "news");
    write_personal_file(root / "collection.toml", "schema_version = 3\nid = 'personal'\nname = 'Personal Assistant'\n");
    write_personal_file(root / "agents/news/agent.toml",
        "schema_version = 3\nid = 'news'\nname = 'News monitor'\nprofile = 'codex'\nrevision = 1\n");
    write_personal_file(root / "agents/news/instructions.md", "Summarize news. Do not send messages.\n");
}
}

TEST_CASE("personal collection strict schema loads and round trips", "[personal][store][protocol]")
{
    TempDir temporary("draxul-personal-store");
    create_collection(temporary.path);
    const auto snapshot = load_personal_agents(temporary.path);
    REQUIRE(snapshot.error.empty());
    REQUIRE(snapshot.agents.size() == 1);
    CHECK(snapshot.agents.front().error.empty());
    std::string error;
    const auto encoded = personal_agents_to_json(snapshot);
    CHECK(personal_agents_from_json(encoded, error) == snapshot);
    auto invalid = encoded;
    invalid["schema_version"] = 999;
    CHECK_FALSE(personal_agents_from_json(invalid, error));
    invalid = encoded;
    invalid["agents"] = "invalid";
    CHECK_FALSE(personal_agents_from_json(invalid, error));
}

TEST_CASE("personal missing and broken sync retain inspectable last known data", "[personal][store]")
{
    TempDir temporary("draxul-personal-sync");
    create_collection(temporary.path);
    const auto good = load_personal_agents(temporary.path);
    SECTION("incomplete manifest")
    {
        write_personal_file(temporary.path / "agents/news/agent.toml", "name = [");
    }
    SECTION("unsupported definition version")
    {
        write_personal_file(temporary.path / "agents/news/agent.toml", "schema_version = 999\n");
    }
    SECTION("oversized instructions")
    {
        write_personal_file(temporary.path / "agents/news/instructions.md", std::string(kPersonalInstructionsLimit + 1, 'x'));
    }
    SECTION("invalid UTF8 instructions")
    {
        write_personal_file(temporary.path / "agents/news/instructions.md", "\xff");
    }
    SECTION("conflicted instructions")
    {
        write_personal_file(temporary.path / "agents/news/instructions (PC's conflicted copy).md", "Conflict");
    }
    SECTION("missing definition directory")
    {
        std::filesystem::remove_all(temporary.path / "agents/news");
    }
    const auto bad = load_personal_agents(temporary.path, good);
    REQUIRE(bad.agents.size() == 1);
    CHECK_FALSE(bad.agents.front().error.empty());
    CHECK(bad.agents.front().instructions == good.agents.front().instructions);
    CHECK(bad.agents.front().name == good.agents.front().name);
}

TEST_CASE("personal collection missing root is not recreated and versions fail closed", "[personal][store]")
{
    TempDir temporary("draxul-personal-missing");
    const auto missing = temporary.path / "missing";
    CHECK_FALSE(load_personal_agents(missing).error.empty());
    CHECK_FALSE(std::filesystem::exists(missing));
    CHECK(load_personal_agents({}).root.empty());
    create_collection(temporary.path);
    const auto good = load_personal_agents(temporary.path);
    write_personal_file(temporary.path / "collection.toml", "schema_version = 999\nid = 'personal'\nname = 'Personal'\n");
    const auto invalid = load_personal_agents(temporary.path, good);
    CHECK_FALSE(invalid.error.empty());
    CHECK(invalid.agents == good.agents);
    CHECK(load_personal_agents(missing, good).agents.empty());
}

TEST_CASE("personal collection rejects outside links", "[personal][store][security]")
{
    TempDir temporary("draxul-personal-link");
    const auto root = temporary.path / "collection";
    create_collection(root);
    const auto outside = temporary.path / "outside.md";
    write_personal_file(outside, "Outside content");
    std::filesystem::remove(root / "agents/news/instructions.md");
    std::error_code error;
    std::filesystem::create_symlink(outside, root / "agents/news/instructions.md", error);
    if (error)
        SKIP("Symbolic link creation unavailable on this machine");
    const auto snapshot = load_personal_agents(root);
    REQUIRE(snapshot.agents.size() == 1);
    CHECK_FALSE(snapshot.agents.front().error.empty());
    CHECK(snapshot.agents.front().instructions.empty());
}

TEST_CASE("personal collection is authenticated and shared across Sessions without runtimes", "[personal][server][integration]")
{
    TempDir temporary("draxul-personal-server");
    const auto root = temporary.path / "collection";
    const auto runtime = temporary.path / "runtime";
    create_collection(root);
    ServerKernel server({ .personal_agents_root = root, .runtime_directory = runtime });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    auto options = probe_options(runtime);
    options.client_id = "personal-client";
    const auto probe = ServerClient::probe(options);
    REQUIRE(probe.ready());
    const auto initial_terminals = server.status_snapshot().terminals;
    const auto request = [&](std::string_view method, nlohmann::json params) {
        return ControlClient::request(namespaced_control_id(kServerControlId, runtime), runtime, method, std::move(params));
    };
    CHECK_FALSE(request("personal.snapshot", nlohmann::json::object()).ok);
    nlohmann::json params{
        { "client_id", options.client_id },
        { "connection_token", probe.welcome->connection_token },
        { "session_id", "first" },
    };
    ControlClientResult first;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        first = request("personal.snapshot", params);
        if (first.ok && first.result["agents"].size() == 1)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(first.ok);
    REQUIRE(first.result["agents"].size() == 1);
    options.client_id = "second-personal-client";
    const auto second_probe = ServerClient::probe(options);
    REQUIRE(second_probe.ready());
    params["client_id"] = options.client_id;
    params["connection_token"] = second_probe.welcome->connection_token;
    params["session_id"] = "second";
    const auto second = request("personal.snapshot", params);
    REQUIRE(second.ok);
    CHECK(first.result == second.result);
    params["agent_id"] = "news";
    CHECK(request("personal.get", params).ok);
    params["agent_id"] = "missing";
    CHECK(request("personal.get", params).error_code == "agent_not_found");
    CHECK_FALSE(request("personal.run", params).ok);
    CHECK(server.status_snapshot().terminals == initial_terminals);
    write_personal_file(root / "agents/news/instructions.md", "Revised instructions.\n");
    bool updated = false;
    for (int attempt = 0; attempt < 150; ++attempt)
    {
        const auto current = request("personal.snapshot", params);
        if (current.ok && current.result["agents"][0]["instructions"] == "Revised instructions.\n")
        {
            updated = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(updated);
    auto recovery = std::make_shared<ClientRecoveryState>("personal-monitor");
    recovery->set_server_identity(second_probe.welcome->server_epoch, second_probe.welcome->connection_token);
    PersonalAgentClient monitor({
        .runtime_directory = runtime,
        .client_id = options.client_id,
        .session_id = "second",
        .recovery = recovery,
    }, {});
    std::shared_ptr<const PersonalAgentSnapshot> observed;
    for (int attempt = 0; attempt < 150; ++attempt)
    {
        observed = monitor.snapshot();
        if (observed && observed->agents.size() == 1 && observed->error.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(observed);
    REQUIRE(observed->agents.size() == 1);
    CHECK(observed->agents.front().instructions == "Revised instructions.\n");
    run_guard.join();
    bool disconnected = false;
    for (int attempt = 0; attempt < 150; ++attempt)
    {
        const auto current = monitor.snapshot();
        if (current && current->error.starts_with("Disconnected"))
        {
            CHECK(current->agents == observed->agents);
            disconnected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(disconnected);
}

#include "personal_agent_service.h"

namespace
{
PersonalAgentSnapshot wait_personal(PersonalAgentService& service, const std::function<bool(const PersonalAgentSnapshot&)>& predicate)
{
    for (int attempt=0;attempt<300;++attempt)
    {
        auto snapshot=service.snapshot();
        if (predicate(snapshot)) return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return service.snapshot();
}
nlohmann::json personal_command_wait(PersonalAgentService& service, nlohmann::json command)
{
    auto snapshot=service.snapshot();
    command["authority"]=snapshot.authority;
    command["collection_id"]=snapshot.collection_id;
    if (!command.contains("request_id")) command["request_id"]=personal_unique_id();
    const auto accepted=service.handle("personal.command",command);
    if (!accepted.ok) return {{"done",true},{"ok",false},{"error",accepted.error_message}};
    for(int i=0;i<300;++i)
    {
        auto result=service.handle("personal.result",{{"request_id",command["request_id"]}});
        if(result.ok && result.value.value("done",false)) return result.value;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return {{"ok",false},{"error","Timed out in test"}};
}
}


TEST_CASE("personal create and rename preserve agent-owned backing files", "[personal][store][server]")
{
    TempDir temp("draxul-personal-metadata");
    create_collection(temp.path);
    PersonalAgentService service(temp.path,temp.path/"local");
    REQUIRE(wait_personal(service,[](const auto& s){return s.agents.size()==1;}).error.empty());
    PersonalAgentDefinition draft; draft.profile="codex";
    auto command=nlohmann::json{{"action","create"},{"request_id","new-chat"},{"definition",personal_definition_to_json(draft)}};
    const auto created=personal_command_wait(service,command);
    REQUIRE(created["ok"]==true);
    CHECK(personal_command_wait(service,command)==created);
    auto agent=personal_definition_from_json(created["result"]["definition"]);
    CHECK(agent.name=="Codex");
    CHECK(agent.revision==1);
    CHECK(std::filesystem::is_directory(temp.path/"agents"/agent.id/"data"));
    const auto folder=temp.path/"agents"/agent.id;
    write_personal_file(folder/"instructions.md","My agent wrote these standing instructions.\n");
    write_personal_file(folder/"state.md","Current task checkpoint.\n");
    const auto snapshot=load_personal_agents(temp.path);
    const auto found=std::ranges::find(snapshot.agents,agent.id,&PersonalAgentDefinition::id);
    REQUIRE(found!=snapshot.agents.end());
    agent=*found;
    auto renamed=agent; renamed.name="My assistant";
    const auto result=personal_command_wait(service,{{"action","rename"},{"definition",personal_definition_to_json(renamed)},
        {"expected",personal_definition_to_json(agent)}});
    REQUIRE(result["ok"]==true);
    CHECK(personal_read_bounded(temp.path,folder/"instructions.md",16384)==agent.instructions);
    CHECK(personal_read_bounded(temp.path,folder/"state.md",16384)=="Current task checkpoint.\n");
    CHECK(personal_command_wait(service,{{"action","rename"},{"definition",personal_definition_to_json(renamed)},
        {"expected",personal_definition_to_json(agent)}})["ok"]==false);
    std::filesystem::remove(folder/"instructions.md");
    const auto missing=load_personal_agents(temp.path);
    const auto boot=std::ranges::find(missing.agents,agent.id,&PersonalAgentDefinition::id);
    REQUIRE(boot!=missing.agents.end());
    CHECK(boot->error.empty()); // The conversational bootstrap can initialize it.
    CHECK(boot->instructions.empty());
}

TEST_CASE("personal native chat owns structured turns across clients and server restarts", "[personal][chat][server][integration]")
{
    TempDir temp("draxul-personal-chat");
    const auto root=temp.path/"collection", runtime=temp.path/"runtime";
    create_collection(root);
    auto exercise=[&](bool resumed) {
        ServerKernel server({.personal_agents_root=root,.personal_local_state=temp.path/"local",.runtime_directory=runtime,
            .agent_definitions={{.profile_id="codex",.kind="codex",.display_name="Test chat",.executable=DRAXUL_PERSONAL_FAKE_PATH}}});
        REQUIRE(server.start().disposition==ServerStartDisposition::Started);
        ServerRunGuard guard(server);
        auto recovery=std::make_shared<ClientRecoveryState>("chat-client");
        ServerControlChannel channel({.runtime_directory=runtime,.client_id="chat-client",.recovery=recovery});
        auto request=[&](std::string_view method,nlohmann::json params) {
            params["session_id"]="default"; return channel.request_with_recovery(method,std::move(params));
        };
        for(int i=0;i<150;++i)
        {
            const auto metadata=request("personal.snapshot",nlohmann::json::object());
            if(metadata.ok && metadata.result["agents"].size()==1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const auto terminal_count=server.status_snapshot().terminals;
        const auto open=request("agent.start",{{"profile_id","codex"},{"personal_id","news"},{"request_id","open-chat"}});
        INFO(open.error_message); REQUIRE(open.ok);
        CHECK(open.result["instance_id"]=="personal-news");
        CHECK(open.result["alias"]=="News monitor");
        CHECK(open.result["route"]["terminal_id"]=="");
        CHECK(server.status_snapshot().terminals==terminal_count);
        auto wait_state=[&](std::string_view desired) {
            ControlClientResult result;
            for(int i=0;i<200;++i)
            {
                result=request("personal.chat.snapshot",{{"agent_id","news"}});
                if(result.ok && result.result["state"].get<std::string>()==desired) return result;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            INFO(result.result.dump()); REQUIRE(false); return result;
        };
        const auto ready=wait_state("idle");
        {
            std::ifstream log(root/"agents/news/received.jsonl");
            std::string line; bool verified=false;
            while(std::getline(log,line))
            {
                const auto request=nlohmann::json::parse(line);
                if(request.value("method","")!=(resumed?"thread/resume":"thread/start")) continue;
                CHECK(request["params"]["sandbox"]=="workspace-write");
                CHECK(request["params"]["approvalPolicy"]=="on-request");
                CHECK(request["params"]["approvalsReviewer"]=="auto_review");
                verified=true;
            }
            CHECK(verified);
        }
        if(resumed)
        {
            REQUIRE(ready.result["messages"].size()>=2);CHECK(ready.result["messages"][1]["text"]=="Answer: hello");
            const auto metadata=request("personal.snapshot",nlohmann::json::object());
            nlohmann::json deletion{{"action","delete"},{"request_id","delete-chat"},
                {"authority",metadata.result["authority"]},{"collection_id",metadata.result["collection_id"]},
                {"expected",metadata.result["agents"][0]},{"confirmed",false}};
            CHECK_FALSE(request("personal.command",deletion).ok);
            deletion["confirmed"]=true;REQUIRE(request("personal.command",deletion).ok);
            ControlClientResult deleted;
            for(int i=0;i<150;++i)
            {
                deleted=request("personal.result",{{"request_id","delete-chat"}});
                if(deleted.ok && deleted.result.value("done",false)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            REQUIRE(deleted.ok);CHECK(deleted.result.value("ok",false));
            CHECK_FALSE(std::filesystem::exists(root/"agents/news"));
            CHECK(request("agent.list",nlohmann::json::object()).result.empty());
            CHECK(server.status_snapshot().terminals==terminal_count);
            CHECK_FALSE(request("personal.chat.open",{{"agent_id","news"}}).ok);
            return;
        }
        nlohmann::json send{{"agent_id","news"},{"action","send"},{"text","hello"},{"request_id","send-first"}};
        REQUIRE(request("personal.chat.command",send).ok);
        REQUIRE(request("personal.chat.command",send).ok); // idempotent, no repeated turn
        auto changed=send;changed["text"]="changed";
        CHECK_FALSE(request("personal.chat.command",changed).ok);
        auto complete=wait_state("idle");
        REQUIRE(complete.result["messages"].size()==2);
        CHECK(complete.result["messages"][0]["text"]=="hello");
        CHECK(complete.result["messages"][1]["text"]=="Answer: hello");
        CHECK(complete.result["tool_count"]==1);
        CHECK(complete.result.dump().find("secret calculation")==std::string::npos);
        CHECK(complete.result.dump().find("hidden reasoning")==std::string::npos);
        CHECK(complete.result.dump().find("I will calculate")==std::string::npos);
        ServerControlChannel observer({.runtime_directory=runtime,.client_id="chat-observer",
            .recovery=std::make_shared<ClientRecoveryState>("chat-observer")});
        CHECK(observer.request_with_recovery("personal.chat.open",{{"agent_id","news"}}).result["messages"]==complete.result["messages"]);
        REQUIRE(request("personal.chat.command",{{"agent_id","news"},{"action","send"},{"text","wait"},{"request_id","send-wait"}}).ok);
        wait_state("working");
        CHECK_FALSE(request("personal.chat.command",{{"agent_id","news"},{"action","send"},{"text","duplicate"},{"request_id","busy-send"}}).ok);
        REQUIRE(request("personal.chat.command",{{"agent_id","news"},{"action","stop"},{"request_id","stop-wait"}}).ok);
        CHECK(wait_state("idle").result["error"]=="Stopped.");
        REQUIRE(request("agent.send_text",{{"instance_id","personal-news"},{"text","approval\r"},{"request_id","ask-approval"}}).ok);
        auto approval=wait_state("approval");
        CHECK(approval.result["approval_text"].get<std::string>().find("echo approved")!=std::string::npos);
        CHECK_FALSE(request("personal.chat.command",{{"agent_id","news"},{"action","approve"},{"approval_id","stale"},{"request_id","stale-approval"}}).ok);
        REQUIRE(request("personal.chat.command",{{"agent_id","news"},{"action","decline"},
            {"approval_id",approval.result["approval_id"]},{"request_id","decline"}}).ok);
        wait_state("idle");
        for(const std::string action:{"approve","decline"})
        {
            REQUIRE(request("personal.chat.command",{{"agent_id","news"},{"action","send"},{"text","permissions"},{"request_id","permissions-"+action}}).ok);
            const auto pending=wait_state("approval");
            CHECK(pending.result["approval_text"].get<std::string>().find("for this turn")!=std::string::npos);
            const auto approval_id=pending.result["approval_id"].get<std::string>();
            CHECK_FALSE(request("personal.chat.command",{{"agent_id","news"},{"action",action},{"approval_id","stale"},{"request_id","stale-permissions-"+action}}).ok);
            REQUIRE(request("personal.chat.command",{{"agent_id","news"},{"action",action},{"approval_id",approval_id},{"request_id","decide-permissions-"+action}}).ok);
            wait_state("idle");
            std::ifstream log(root/"agents/news/received.jsonl");
            std::string line;bool verified=false;
            while(std::getline(log,line))
            {
                const auto wire=nlohmann::json::parse(line);
                if(!wire.contains("result") || wire["id"].dump()!=approval_id) continue;
                CHECK(wire["result"]["scope"]=="turn");
                if(action=="approve")
                {
                    CHECK(wire["result"]["permissions"]["network"]["enabled"]==true);
                    const auto entries=wire["result"]["permissions"]["fileSystem"]["entries"];
                    REQUIRE(entries.size()==1);CHECK(entries[0]["access"]=="read");
                    CHECK(entries[0]["path"]["path"]==std::filesystem::canonical(root/"agents/news").string());
                }
                else CHECK(wire["result"]["permissions"].empty());
                CHECK_FALSE(wire["result"].contains("decision"));verified=true;
            }
            CHECK(verified);
        }
        const auto again=request("agent.start",{{"profile_id","codex"},{"personal_id","news"},{"request_id","reopen-chat"}});
        REQUIRE(again.ok);CHECK(again.result["route"]["pane_id"]==open.result["route"]["pane_id"]);
        const auto listed=request("agent.list",nlohmann::json::object());
        REQUIRE(listed.ok);REQUIRE(listed.result.size()==1);CHECK(listed.result[0]["alias"]=="News monitor");
        CHECK(server.status_snapshot().terminals==terminal_count);
    };
    exercise(false);exercise(true);
    const auto received=personal_read_bounded(root,root/".deleted/news/received.jsonl",65536);
    CHECK(received.find("thread/resume")!=std::string::npos);
    CHECK(received.find("instructions.md")!=std::string::npos);
    CHECK(std::filesystem::exists(temp.path/"local/chat/personal/news.json"));
}

TEST_CASE("personal chat automatically replaces malformed and exited providers without affecting the server", "[personal][chat][server][integration]")
{
    TempDir temp("draxul-personal-chat-error");
    create_collection(temp.path/"collection");
    std::string prompt,provider="codex";
    SECTION("unsupported personal provider"){provider="claude";}
    SECTION("provider exit"){prompt="crash";}
    SECTION("invalid JSON"){prompt="malformed";}
    ServerKernel server({.personal_agents_root=temp.path/"collection",.personal_local_state=temp.path/"local",
        .runtime_directory=temp.path/"runtime",.agent_definitions={{.profile_id="codex",.kind=provider,
        .display_name="Test",.executable=DRAXUL_PERSONAL_FAKE_PATH}}});
    REQUIRE(server.start().disposition==ServerStartDisposition::Started);ServerRunGuard guard(server);
    ServerControlChannel channel({.runtime_directory=temp.path/"runtime",.client_id="chat-error",
        .recovery=std::make_shared<ClientRecoveryState>("chat-error")});
    auto call=[&](std::string_view method,nlohmann::json params){return channel.request_with_recovery(method,std::move(params));};
    ControlClientResult state;
    for(int i=0;i<200;++i)
    {
        state=call("personal.chat.open",{{"agent_id","news"}});
        if((state.ok && state.result["state"]=="idle") || state.error_code=="unsupported_provider") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if(provider=="claude") {CHECK_FALSE(state.ok);CHECK(state.error_code=="unsupported_provider");return;}
    REQUIRE(state.ok);REQUIRE(state.result["state"]=="idle");
    REQUIRE(call("personal.chat.command",{{"agent_id","news"},{"action","send"},{"text",prompt},{"request_id","failing-turn"}}).ok);
    bool reconnecting=false;
    for(int i=0;i<500;++i)
    {
        state=call("personal.chat.snapshot",{{"agent_id","news"}});
        if(state.ok && state.result["state"]=="connecting") reconnecting=true;
        if(reconnecting && state.ok && state.result["state"]=="idle") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(state.ok);CHECK(reconnecting);CHECK(state.result["state"]=="idle");CHECK(state.result["error"]=="");
    CHECK(call("server.status",nlohmann::json::object()).ok);
    const auto& messages=state.result["messages"];
    CHECK(std::ranges::count_if(messages,[&](const auto& message){return message["role"]=="user" && message["text"]==prompt;})==1);
    CHECK(std::ranges::count_if(messages,[](const auto& message){return message["role"]=="assistant";})==0);
    REQUIRE(call("personal.chat.command",{{"agent_id","news"},{"action","send"},{"text","hello"},{"request_id","after-recovery"}}).ok);
    for(int i=0;i<200;++i)
    {
        state=call("personal.chat.snapshot",{{"agent_id","news"}});
        if(state.ok && state.result["state"]=="idle") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(state.result["state"]=="idle");CHECK(state.result["messages"].back()["text"]=="Answer: hello");
    // Ordinary shells remain usable while the personal process is replaced.
    auto terminal=remote_client(temp.path/"runtime","ordinary-shell",server.status_snapshot().server_epoch,
        "terminal",std::string(kServerShellTerminalId));
    std::string error;REQUIRE(terminal.attach(error));
    REQUIRE(terminal.send_input("echo DRAXUL_ORDINARY_SHELL_OK\r",error));
    REQUIRE(wait_for_text(terminal,"DRAXUL_ORDINARY_SHELL_OK",error));

}

#include "personal_chat_service.h"

TEST_CASE("personal schedule checks wake unopened agents and coalesce behind active turns", "[personal][chat][server][integration]")
{
    TempDir temp("draxul-personal-schedule");
    const auto root=temp.path/"collection";
    create_collection(root);
    std::filesystem::create_directories(root/"agents/second");
    write_personal_file(root/"agents/second/agent.toml",
        "schema_version = 3\nid = 'second'\nname = 'Second'\nprofile = 'codex'\nrevision = 1\n");
    std::vector<AgentDefinition> profiles{{.profile_id="codex",.kind="codex",.display_name="Test",.executable=DRAXUL_PERSONAL_FAKE_PATH}};
    PersonalAgentService metadata(root,temp.path/"local",profiles);
    PersonalChatService chat(metadata,temp.path/"local",profiles);
    for(int i=0;i<200 && metadata.snapshot().agents.size()!=2;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(metadata.snapshot().agents.size()==2);
    auto now=std::chrono::steady_clock::now();
    chat.tick(now);
    CHECK(chat.handle("personal.chat.snapshot",{{"agent_id","news"}}).ok); // Startup prepares unopened identities.
    auto wait=[&](std::string id, uint64_t checks, std::string state="idle") {
        ControlMethodResult result;
        for(int i=0;i<300;++i)
        {
            result=chat.handle("personal.chat.snapshot",{{"agent_id",id}});
            if(result.ok && result.value["state"]==state && result.value["schedule_checks"]==checks) return result.value;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        INFO(result.value.dump());REQUIRE(false);return result.value;
    };
    CHECK(wait("news",0)["messages"].empty());
    CHECK(wait("second",0)["messages"].empty());
    now+=std::chrono::minutes(5);chat.tick(now);
    CHECK(wait("news",1)["messages"].empty());
    CHECK(wait("second",1)["messages"].empty());
    auto request=[&](std::string action,std::string id,std::string text="") {
        return chat.handle("personal.chat.command",{{"agent_id","news"},{"action",action},{"request_id",id},{"text",text}});
    };
    REQUIRE(request("send","busy","wait").ok);
    wait("news",1,"working");
    now+=std::chrono::minutes(5);chat.tick(now);
    now+=std::chrono::minutes(5);chat.tick(now);
    const auto busy=wait("news",1,"working");
    CHECK(busy["schedule_checks"]==1);
    // Stop explicitly cancels the deferred check; a later cadence can wake again.
    REQUIRE(request("stop","cancel").ok);
    CHECK(wait("news",1)["messages"].size()==1);
    write_personal_file(root/"agents/news/due.txt","due");
    now+=std::chrono::minutes(5);chat.tick(now);
    const auto completed=wait("news",2);
    REQUIRE(completed["messages"].size()==2);
    CHECK(completed["messages"][1]["text"]=="Scheduled work completed.");
    CHECK_FALSE(completed["last_schedule_check"].get<std::string>().empty());
    // Repeated pumps at the same instant do not create duplicate turns.
    chat.tick(now);chat.tick(now);
    CHECK(wait("news",2)["messages"].size()==2);
    REQUIRE(request("send","ordinary","hello").ok);
    CHECK(wait("news",2)["messages"].back()["text"]=="Answer: hello");
    write_personal_file(root/"agents/news/invalid-schedule.txt","invalid");
    now+=std::chrono::minutes(5);chat.tick(now);
    CHECK(wait("news",3)["error"]=="Schedule check returned an invalid result.");
    // Multiple ticks while approval-blocked produce exactly one deferred check.
    std::filesystem::remove(root/"agents/news/invalid-schedule.txt");
    std::filesystem::remove(root/"agents/news/due.txt");
    REQUIRE(request("send","blocked","approval").ok);
    const auto approval=wait("news",3,"approval");
    now+=std::chrono::minutes(5);chat.tick(now);
    now+=std::chrono::minutes(5);chat.tick(now);
    CHECK(wait("news",3,"approval")["schedule_checks"]==3);
    REQUIRE(chat.handle("personal.chat.command",{{"agent_id","news"},{"action","decline"},
        {"approval_id",approval["approval_id"]},{"request_id","release-blocked"}}).ok);
    const auto deferred=wait("news",4);
    CHECK(deferred["messages"].size()==approval["messages"].size());
    // Removing an identity never recreates its folder on subsequent checks.
    std::filesystem::remove_all(root/"agents/news");
    const auto missing=wait_personal(metadata,[](const auto& snapshot){
        return std::ranges::any_of(snapshot.agents,[](const auto& agent){return agent.id=="news" && !agent.error.empty();});
    });
    REQUIRE(std::ranges::any_of(missing.agents,[](const auto& agent){return agent.id=="news" && !agent.error.empty();}));
    now+=std::chrono::minutes(5);chat.tick(now);
    CHECK_FALSE(std::filesystem::exists(root/"agents/news"));
}

TEST_CASE("unstarted personal chats reopen without resuming a nonexistent rollout", "[personal][chat][server][integration]")
{
    TempDir temp("draxul-personal-unstarted");
    const auto root=temp.path/"collection";
    create_collection(root);
    std::vector<AgentDefinition> profiles{{.profile_id="codex",.kind="codex",.display_name="Test",.executable=DRAXUL_PERSONAL_FAKE_PATH}};
    PersonalAgentService metadata(root,temp.path/"local",profiles);
    REQUIRE(wait_personal(metadata,[](const auto& s){return s.agents.size()==1;}).error.empty());
    for(int restart=0;restart<2;++restart)
    {
        PersonalChatService chat(metadata,temp.path/"local",profiles);
        REQUIRE(chat.handle("personal.chat.open",{{"agent_id","news"}}).ok);
        ControlMethodResult ready;
        for(int i=0;i<300;++i)
        {
            ready=chat.handle("personal.chat.snapshot",{{"agent_id","news"}});
            if(ready.ok && ready.value["state"]!="connecting") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        INFO(ready.value.dump());REQUIRE(ready.ok);REQUIRE(ready.value["state"]=="idle");
        CHECK(ready.value["error"]=="");
        const auto saved=nlohmann::json::parse(personal_read_bounded(temp.path,temp.path/"local/chat/personal/news.json",65536));
        CHECK(saved["thread_id"]=="");
        CHECK(saved["messages"].empty());
    }
    const auto received=personal_read_bounded(root,root/"agents/news/received.jsonl",65536);
    CHECK(received.find("thread/resume")==std::string::npos);
    CHECK(personal_read_bounded(root,root/"agents/news/instructions.md",65536)=="Summarize news. Do not send messages.\n");
}

TEST_CASE("personal assistants rebuild missing provider history from their backing folder", "[personal][chat][server][integration]")
{
    TempDir temp("draxul-personal-missing-history");
    const auto root=temp.path/"collection";
    create_collection(root);
    std::vector<AgentDefinition> profiles{{.profile_id="codex",.kind="codex",.display_name="Test",.executable=DRAXUL_PERSONAL_FAKE_PATH}};
    const auto saved=temp.path/"local/chat/personal/news.json";
    std::filesystem::create_directories(saved.parent_path());
    personal_write_atomic(saved,nlohmann::json{{"schema",1},{"profile","codex"},{"thread_id","missing-thread"},
        {"messages",nlohmann::json::array({{{"id","old-message"},{"role","assistant"},{"text","Earlier answer"}}})},{"interrupted",false}}.dump());
    PersonalAgentService metadata(root,temp.path/"local",profiles);
    REQUIRE(wait_personal(metadata,[](const auto& s){return s.agents.size()==1;}).error.empty());
    PersonalChatService chat(metadata,temp.path/"local",profiles);
    chat.tick(std::chrono::steady_clock::now());
    ControlMethodResult state;
    for(int i=0;i<300;++i)
    {
        state=chat.handle("personal.chat.snapshot",{{"agent_id","news"}});
        if(state.ok && state.value["state"]=="idle") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    INFO(state.value.dump());REQUIRE(state.ok);REQUIRE(state.value["state"]=="idle");CHECK(state.value["error"]=="");
    REQUIRE(state.value["messages"].size()==1);CHECK(state.value["messages"][0]["text"]=="Earlier answer");
    const auto wire=personal_read_bounded(root,root/"agents/news/received.jsonl",65536);
    CHECK(wire.find("thread/resume")!=std::string::npos);CHECK(wire.find("thread/start")!=std::string::npos);
    CHECK(wire.find("instructions.md")!=std::string::npos);
    CHECK(personal_read_bounded(root,root/"agents/news/instructions.md",65536)=="Summarize news. Do not send messages.\n");
}
