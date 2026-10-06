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

TEST_CASE("personal pill launches one interactive terminal with backing bootstrap", "[personal][server][integration]")
{
    TempDir temp("draxul-personal-chat");
    const auto root=temp.path/"collection";
    const auto runtime=temp.path/"runtime";
    create_collection(root);
    std::string provider="codex";
    bool explicit_model=false;
    bool final_tab=false;
    SECTION("Codex default") {}
    SECTION("Delete the final tab") { final_tab=true; }
    SECTION("Codex profile override") { explicit_model=true; }
    SECTION("Claude") { provider="claude"; explicit_model=true; }
    ServerKernel server({.personal_agents_root=root,.personal_local_state=temp.path/"local",.runtime_directory=runtime,
        .agent_definitions={{.profile_id="codex",.kind=provider,.display_name="Test chat",.executable=DRAXUL_PERSONAL_FAKE_PATH,
            .default_args=explicit_model ? std::vector<std::string>{"--model","profile-model"} : std::vector<std::string>{}}}});
    REQUIRE(server.start().disposition==ServerStartDisposition::Started);
    ServerRunGuard guard(server);
    auto recovery=std::make_shared<ClientRecoveryState>("personal-chat-client");
    ServerControlChannel channel({.runtime_directory=runtime,.client_id="personal-chat-client",.recovery=recovery});
    auto request=[&](std::string_view method,nlohmann::json params) {
        params["session_id"]="default";
        return channel.request_with_recovery(method,std::move(params));
    };
    for(int i=0;i<150;++i)
    {
        const auto snapshot=request("personal.snapshot",nlohmann::json::object());
        if(snapshot.ok && snapshot.result["agents"].size()==1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto before=request("topology.snapshot",nlohmann::json::object());
    REQUIRE(before.ok);
    const auto original_tab=before.result["spaces"][0]["tabs"][0]["tab_id"];
    const auto start=request("agent.start",{{"profile_id","codex"},{"personal_id","news"},{"request_id","chat-start"}});
    INFO(start.error_message);
    REQUIRE(start.ok);
    CHECK(start.result["instance_id"]=="personal-news");
    CHECK(start.result["route"]["tab_id"]!=original_tab);
    const auto after=request("topology.snapshot",nlohmann::json::object());
    REQUIRE(after.ok);
    CHECK(after.result["spaces"][0]["tabs"].size()==2);
    CHECK(after.result["spaces"][0]["tabs"][1]["panes"].size()==1);
    const auto terminals=server.status_snapshot().terminals;
    const auto repeated=request("agent.start",{{"profile_id","codex"},{"personal_id","news"},{"request_id","chat-reopen"}});
    REQUIRE(repeated.ok);
    CHECK(repeated.result["instance_id"]==start.result["instance_id"]);
    CHECK(server.status_snapshot().terminals==terminals);
    const auto launch_file=root/"agents/news/launch.json";
    for(int i=0;i<150 && !std::filesystem::exists(launch_file);++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(std::filesystem::exists(launch_file));
    const auto args=nlohmann::json::parse(personal_read_bounded(root,launch_file,16384)).get<std::vector<std::string>>();
    REQUIRE(args.size()>=4);
    CHECK(args[0]=="--model");
    CHECK(args[1]==(explicit_model ? "profile-model" : "gpt-6.1-sol"));
    CHECK(args.back().find("instructions.md")!=std::string::npos);
    CHECK(args.back().find("state.md")!=std::string::npos);
    CHECK(args.back().find("agents")!=std::string::npos);
    CHECK(args.back().find("news")!=std::string::npos);
    CHECK(std::ranges::find(args,"exec")==args.end());
    CHECK(std::ranges::find(args,"--print")==args.end());
    REQUIRE(request("agent.send_text",{{"instance_id","personal-news"},{"text","hello personal chat"}}).ok);
    REQUIRE(request("agent.send_keys",{{"instance_id","personal-news"},{"keys",{"enter"}}}).ok);
    const auto received=root/"agents/news/received.txt";
    for(int i=0;i<150 && !std::filesystem::exists(received);++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(std::filesystem::exists(received));
    CHECK(personal_read_bounded(root,received,4096).find("hello personal chat")!=std::string::npos);
    // The process belongs to the server and remains available to another client.
    auto observer_recovery=std::make_shared<ClientRecoveryState>("personal-observer");
    ServerControlChannel observer({.runtime_directory=runtime,.client_id="personal-observer",.recovery=observer_recovery});
    const auto observed=observer.request_with_recovery("agent.get",{{"session_id","default"},{"instance_id","personal-news"}});
    REQUIRE(observed.ok);
    CHECK(observed.result["running"]==true);
    const auto bad=request("agent.start",{{"profile_id","codex"},{"personal_id","../outside"},{"request_id","bad-chat"}});
    CHECK_FALSE(bad.ok);
    if (final_tab)
    {
        const auto topology=request("topology.snapshot",nlohmann::json::object());
        REQUIRE(topology.ok);
        const auto closed=request("topology.command",topology_command_to_json({
            .client_id="personal-chat-client",.command_id="close-original",
            .expected_revision=topology.result["revision"].get<uint64_t>(),
            .kind=TopologyCommandKind::CloseTab,
            .space_id=topology.result["spaces"][0]["space_id"].get<std::string>(),
            .tab_id=original_tab.get<std::string>()}));
        REQUIRE(closed.ok);
    }
    const auto deletion_terminals=server.status_snapshot().terminals;
    const auto collection=request("personal.snapshot",nlohmann::json::object());
    REQUIRE(collection.ok);
    nlohmann::json deletion{{"action","delete"},{"request_id","delete-personal-news"},
        {"authority",collection.result["authority"]},{"collection_id",collection.result["collection_id"]},
        {"expected",collection.result["agents"][0]},{"confirmed",false}};
    CHECK_FALSE(request("personal.command",deletion).ok);
    CHECK(std::filesystem::exists(root/"agents/news"));
    CHECK(server.status_snapshot().terminals==deletion_terminals);
    deletion["confirmed"]=true;
    REQUIRE(request("personal.command",deletion).ok);
    ControlClientResult deleted;
    for(int i=0;i<150;++i)
    {
        deleted=request("personal.result",{{"request_id","delete-personal-news"}});
        if(deleted.ok && deleted.result.value("done",false)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(deleted.ok); INFO(deleted.result.dump());
    REQUIRE(deleted.result.value("ok",false));
    CHECK_FALSE(std::filesystem::exists(root/"agents/news"));
    CHECK(std::filesystem::exists(root/".deleted/news/instructions.md"));
    CHECK(server.status_snapshot().terminals==(final_tab ? deletion_terminals : deletion_terminals-1));
    CHECK(request("personal.command",deletion).result==deleted.result);
    const auto final_snapshot=request("personal.snapshot",nlohmann::json::object());
    REQUIRE(final_snapshot.ok);
    CHECK(final_snapshot.result["agents"].empty());
}
