#include <catch2/catch_all.hpp>

#include "support/temp_dir.h"

#include <draxul/agent_integration.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };
}

void write_text(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(static_cast<bool>(output << contents));
}

AgentIntegrationResult apply(AgentIntegrationProvider provider,
    AgentIntegrationAction action, const AgentIntegrationPaths& paths)
{
    return apply_agent_integration({
        .provider = provider,
        .action = action,
        .paths = paths,
    });
}

} // namespace

TEST_CASE("Codex explicit-path installation is idempotent and preserves configuration",
    "[agent-integration][codex]")
{
    TempDir temp("draxul-agent-integration-codex");
    const auto paths = agent_integration_paths(
        AgentIntegrationProvider::Codex, temp.path);
    write_text(paths.registration,
        R"({"hooks":{"Stop":[{"hooks":[{"type":"command","command":"keep-me"}]}]}})");
    write_text(paths.features,
        "model = \"gpt-5\"\n  [features]\nhooks_extra = false\nother = true\n");

    REQUIRE(apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths)
            .success);
    const auto second = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths);
    REQUIRE(second.success);
    CHECK(second.status.state == AgentIntegrationState::Current);
    CHECK(second.status.expected_version == 2);
    CHECK(second.status.path == paths.hook);

    const auto hook = read_text(paths.hook);
    CHECK(hook.find("DRAXUL_INTEGRATION_ID=codex") != std::string::npos);
    CHECK(hook.find("DRAXUL_INTEGRATION_VERSION=2") != std::string::npos);
    CHECK(hook.find("draxul:codex") != std::string::npos);
    CHECK(hook.find("DRAXUL_SERVER_EPOCH") != std::string::npos);
    CHECK(hook.find("runtime-generation") != std::string::npos);
#ifdef _WIN32
    CHECK(hook.find("powershell.exe") == std::string::npos);
#else
    const auto permissions = std::filesystem::status(paths.hook).permissions();
    CHECK((permissions & std::filesystem::perms::owner_exec)
        != std::filesystem::perms::none);
#endif

    const auto registration = nlohmann::json::parse(read_text(paths.registration));
    CHECK(registration["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
    REQUIRE(registration["hooks"]["SessionStart"].size() == 1);
    const auto command = registration["hooks"]["SessionStart"][0]["hooks"][0]
                                     ["command"]
                                         .get<std::string>();
#ifdef _WIN32
    CHECK(command.find("powershell.exe -NoProfile -ExecutionPolicy Bypass")
        != std::string::npos);
#else
    CHECK(command == "\"" + paths.hook.string() + "\" session");
#endif

    const auto config = read_text(paths.features);
    CHECK(config.find("model = \"gpt-5\"") != std::string::npos);
    CHECK(config.find(
              "  [features]\nhooks = true\nhooks_extra = false\nother = true")
        != std::string::npos);
    CHECK(config.find("hooks = true", config.find("hooks = true") + 1)
        == std::string::npos);

    const auto removed = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Uninstall, paths);
    REQUIRE(removed.success);
    CHECK(removed.status.state == AgentIntegrationState::NotInstalled);
    CHECK_FALSE(std::filesystem::exists(paths.hook));
    const auto remaining = nlohmann::json::parse(read_text(paths.registration));
    CHECK(remaining["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
    CHECK(remaining["hooks"]["SessionStart"].empty());
}

TEST_CASE("Codex installation understands commented and quoted TOML sections",
    "[agent-integration][codex]")
{
    TempDir temp("draxul-agent-integration-toml-sections");
    const auto paths = agent_integration_paths(
        AgentIntegrationProvider::Codex, temp.path);
    write_text(paths.features,
        "model = \"gpt-5\"\n"
        "[\"features\"] # keep this comment\n"
        "other = true\n"
        "[other] # neighboring table\n"
        "hooks = false\n");

    REQUIRE(apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths)
            .success);
    const auto first = read_text(paths.features);
    CHECK(first.find("[\"features\"] # keep this comment\nhooks = true\nother = true")
        != std::string::npos);
    CHECK(first.find("[other] # neighboring table\nhooks = false")
        != std::string::npos);
    CHECK(inspect_agent_integration(AgentIntegrationProvider::Codex, paths).state
        == AgentIntegrationState::Current);

    REQUIRE(apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths)
            .success);
    CHECK(read_text(paths.features) == first);
}

TEST_CASE("Codex installation updates existing TOML values and refuses invalid config",
    "[agent-integration][codex]")
{
    TempDir temp("draxul-agent-integration-toml-values");
    const auto paths = agent_integration_paths(
        AgentIntegrationProvider::Codex, temp.path);
    write_text(paths.features,
        "features.hooks = false # deliberate disable\n"
        "[other]\nhooks = false\n");
    REQUIRE(apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths)
            .success);
    CHECK(read_text(paths.features).find(
              "features.hooks = true # deliberate disable\n"
              "[other]\nhooks = false")
        != std::string::npos);

    write_text(paths.features, "[features] # duplicate-prone header\nvalue = true\n[broken\n");
    const auto original = read_text(paths.features);
    const auto invalid = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths);
    CHECK_FALSE(invalid.success);
    CHECK(invalid.error == "Codex config.toml is invalid TOML.");
    CHECK(read_text(paths.features) == original);

    write_text(paths.features, "[features]\nhooks = \"no\"\n");
    const auto wrong_type = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths);
    CHECK_FALSE(wrong_type.success);
    CHECK(wrong_type.error == "Codex config.toml 'features.hooks' must be a boolean.");
    CHECK(read_text(paths.features) == "[features]\nhooks = \"no\"\n");
}

TEST_CASE("Codex installation preserves inline and implicit feature tables",
    "[agent-integration][codex]")
{
    TempDir temp("draxul-agent-integration-toml-tables");
    const auto paths = agent_integration_paths(
        AgentIntegrationProvider::Codex, temp.path);

    write_text(paths.features, "features = { other = true }\nmodel = \"gpt-5\"\n");
    REQUIRE(apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths)
            .success);
    CHECK(read_text(paths.features).find("other = true") != std::string::npos);
    CHECK(read_text(paths.features).find("gpt-5") != std::string::npos);
    CHECK(inspect_agent_integration(AgentIntegrationProvider::Codex, paths).state
        == AgentIntegrationState::Current);

    write_text(paths.features, "[features.extra]\nkeep = true\n");
    REQUIRE(apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, paths)
            .success);
    CHECK(read_text(paths.features).find("keep = true") != std::string::npos);
    CHECK(inspect_agent_integration(AgentIntegrationProvider::Codex, paths).state
        == AgentIntegrationState::Current);
}

TEST_CASE("Claude explicit-path installation keeps matcher and payload semantics distinct",
    "[agent-integration][claude]")
{
    TempDir temp("draxul-agent-integration-claude");
    const auto paths = agent_integration_paths(
        AgentIntegrationProvider::Claude, temp.path);
    write_text(paths.registration, R"({
  "permissions": {"allow": ["Read"]},
  "hooks": {"Stop": [{"hooks": [{"type": "command", "command": "keep-me"}]}]}
})");

    REQUIRE(apply(AgentIntegrationProvider::Claude,
        AgentIntegrationAction::Install, paths)
            .success);
    const auto second = apply(AgentIntegrationProvider::Claude,
        AgentIntegrationAction::Install, paths);
    REQUIRE(second.success);
    CHECK(second.status.state == AgentIntegrationState::Current);

    const auto hook = read_text(paths.hook);
    CHECK(hook.find("DRAXUL_INTEGRATION_ID=claude") != std::string::npos);
    CHECK(hook.find("draxul:claude") != std::string::npos);
    CHECK(hook.find("agent_id") != std::string::npos);
    CHECK(hook.find("--ref-kind") != std::string::npos);
    CHECK(hook.find("DRAXUL_SERVER_EPOCH") != std::string::npos);
    CHECK(hook.find("runtime-generation") != std::string::npos);

    const auto settings = nlohmann::json::parse(read_text(paths.registration));
    CHECK(settings["permissions"]["allow"][0] == "Read");
    CHECK(settings["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
    REQUIRE(settings["hooks"]["SessionStart"].size() == 1);
    CHECK(settings["hooks"]["SessionStart"][0]["matcher"] == "*");

    const auto removed = apply(AgentIntegrationProvider::Claude,
        AgentIntegrationAction::Uninstall, paths);
    REQUIRE(removed.success);
    CHECK(removed.status.state == AgentIntegrationState::NotInstalled);
    CHECK_FALSE(std::filesystem::exists(paths.hook));
    const auto remaining = nlohmann::json::parse(read_text(paths.registration));
    CHECK(remaining["permissions"]["allow"][0] == "Read");
    CHECK(remaining["hooks"]["Stop"][0]["hooks"][0]["command"] == "keep-me");
}

TEST_CASE("integration reports malformed documents and hook versions through typed status",
    "[agent-integration][status]")
{
    TempDir codex_temp("draxul-agent-integration-invalid-codex");
    const auto codex = agent_integration_paths(
        AgentIntegrationProvider::Codex, codex_temp.path);
    write_text(codex.registration, "[]\n");
    const auto codex_result = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, codex);
    CHECK_FALSE(codex_result.success);
    CHECK(codex_result.error == "Codex hooks.json is not a JSON object.");
    CHECK(codex_result.status.state == AgentIntegrationState::Invalid);
    CHECK(codex_result.status.reason == "Codex hooks.json is invalid.");

    write_text(codex.registration,
        R"({"hooks":{"SessionStart":[{"hooks":[{"type":"command","command":7}]}]}})");
    const auto odd_entry = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, codex);
    REQUIRE(odd_entry.success);
    CHECK(odd_entry.status.state == AgentIntegrationState::Current);

    TempDir claude_temp("draxul-agent-integration-invalid-claude");
    const auto claude = agent_integration_paths(
        AgentIntegrationProvider::Claude, claude_temp.path);
    write_text(claude.registration, "not json\n");
    const auto claude_result = apply(AgentIntegrationProvider::Claude,
        AgentIntegrationAction::Install, claude);
    CHECK_FALSE(claude_result.success);
    CHECK(claude_result.error == "Claude settings.json is not a JSON object.");
    CHECK(claude_result.status.state == AgentIntegrationState::Invalid);
    CHECK(claude_result.status.reason == "Claude settings.json is invalid.");

    write_text(claude.hook,
        "# DRAXUL_INTEGRATION_ID=claude\n# DRAXUL_INTEGRATION_VERSION=1\n");
    CHECK(inspect_agent_integration(AgentIntegrationProvider::Claude, claude).state
        == AgentIntegrationState::Outdated);
    CHECK(inspect_agent_integration(AgentIntegrationProvider::Claude, claude)
              .expected_version
        == 2);
}

TEST_CASE("uninstall removes only owned hooks and filesystem failures are typed results",
    "[agent-integration][filesystem]")
{
    TempDir temp("draxul-agent-integration-filesystem");
    const auto paths = agent_integration_paths(
        AgentIntegrationProvider::Codex, temp.path);
    write_text(paths.hook, "#!/bin/sh\n# user-owned hook\n");

    const auto refusal = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Uninstall, paths);
    CHECK_FALSE(refusal.success);
    CHECK(refusal.error == "Refusing to remove a hook not owned by Draxul.");
    CHECK(std::filesystem::exists(paths.hook));

    auto unwritable = paths;
    unwritable.hook = temp.path / "missing-parent" / paths.hook.filename();
    const auto failed = apply(AgentIntegrationProvider::Codex,
        AgentIntegrationAction::Install, unwritable);
    CHECK_FALSE(failed.success);
    CHECK(failed.error == "Unable to write " + paths.hook.filename().string() + ".");

    const auto missing = agent_integration_paths(
        AgentIntegrationProvider::Claude, temp.path / "absent");
    const auto missing_result = apply(AgentIntegrationProvider::Claude,
        AgentIntegrationAction::Install, missing);
    CHECK_FALSE(missing_result.success);
    CHECK(missing_result.error
        == "Claude config directory was not found. Install Claude first.");
    CHECK(missing_result.status.state == AgentIntegrationState::Unavailable);
}
