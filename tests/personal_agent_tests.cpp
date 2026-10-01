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
    write_personal_file(root / "collection.toml", "schema_version = 1\nid = 'personal'\nname = 'Personal Assistant'\n");
    write_personal_file(root / "agents/news/agent.toml",
        "schema_version = 1\nid = 'news'\nname = 'News monitor'\nprofile = 'codex'\nmodel = 'chosen-model'\nrevision = 1\nenabled = true\ninterval_seconds = 600\n");
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
    CHECK(snapshot.agents.front().enabled);
    CHECK(snapshot.agents.front().interval_seconds == 600);
    std::string error;
    const auto encoded = personal_agents_to_json(snapshot);
    CHECK(encoded["execution_enabled"] == false);
    CHECK(personal_agents_from_json(encoded, error) == snapshot);
    auto invalid = encoded;
    invalid["schema_version"] = 2;
    CHECK_FALSE(personal_agents_from_json(invalid, error));
    invalid = encoded;
    invalid["execution_enabled"] = true;
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
    SECTION("missing instructions")
    {
        std::filesystem::remove(temporary.path / "agents/news/instructions.md");
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
    write_personal_file(temporary.path / "collection.toml", "schema_version = 2\nid = 'personal'\nname = 'Personal'\n");
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
    CHECK(second.result["execution_enabled"] == false);
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
