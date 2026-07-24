#include <catch2/catch_all.hpp>

#include <draxul/agent_model.h>

using namespace draxul;

TEST_CASE("agent discovery recognizes direct and structured wrapper processes",
    "[agent][discovery]")
{
    AgentProcessObservation direct{
        .processes = { {
            .process_id = 42,
            .executable = "C:/tools/codex.exe",
        } },
        .foreground_reliable = false,
    };
    auto match = discover_agent_process(direct);
    REQUIRE(match);
    CHECK(match->kind == "codex");
    CHECK(match->evidence_category == "direct_executable");
    CHECK_FALSE(match->high_confidence);

    AgentProcessObservation wrapper{
        .processes = { {
            .process_id = 43,
            .executable = "/usr/bin/node",
            .arguments = { "node",
                "/opt/node_modules/@anthropic-ai/claude-code/cli.js" },
        } },
        .foreground_reliable = true,
    };
    match = discover_agent_process(wrapper);
    REQUIRE(match);
    CHECK(match->kind == "claude");
    CHECK(match->evidence_category == "structured_wrapper");
    CHECK(match->high_confidence);
}

TEST_CASE("agent discovery accepts explicit hints and rejects competing agents",
    "[agent][discovery]")
{
    AgentProcessObservation hinted{
        .processes = { {
            .process_id = 7,
            .executable = "/opt/vm-wrapper",
            .agent_hint = "codex",
        } },
        .foreground_reliable = true,
    };
    auto match = discover_agent_process(hinted);
    REQUIRE(match);
    CHECK(match->kind == "codex");
    CHECK(match->evidence_category == "environment_hint");

    hinted.processes.push_back({
        .process_id = 8,
        .executable = "/usr/local/bin/claude",
    });
    CHECK_FALSE(discover_agent_process(hinted));

    AgentProcessObservation ambiguous_windows{
        .processes = {
            {
                .process_id = 20,
                .parent_process_id = 10,
                .executable = "C:/tools/codex.exe",
            },
            {
                .process_id = 21,
                .parent_process_id = 10,
                .executable = "C:/tools/codex.exe",
            },
        },
        .foreground_reliable = false,
    };
    CHECK_FALSE(discover_agent_process(ambiguous_windows));
    ambiguous_windows.processes[1].parent_process_id = 20;
    REQUIRE(discover_agent_process(ambiguous_windows));
}
