#include <catch2/catch_test_macros.hpp>

#include "server_status_surface.h"

using namespace draxul;

TEST_CASE("server status surface formats bounded operational counts",
    "[server][status-surface][slice9]")
{
    ServerStatusSnapshot status{
        .state = "ready",
        .connected_clients = 2,
        .sessions = 2,
        .spaces = 4,
        .terminals = 5,
        .agents = 1,
        .session_statuses = {
            {
                .session_id = "default",
                .terminals = 3,
                .live_terminals = 2,
            },
            {
                .session_id = "work",
                .terminals = 2,
                .live_terminals = 1,
            },
        },
    };

    const auto text = format_server_status_text(status);
    CHECK(text.state == "Draxul Server - ready");
    CHECK(text.clients_and_terminals
        == "2 clients - 3 live terminals / 5 terminals");
    CHECK(text.sessions_spaces_and_agents
        == "2 Sessions - 4 Spaces - 1 Agent");
    CHECK(format_server_status_summary(status)
        == "Draxul Server - ready; 2 clients - 3 live terminals / 5 terminals; "
           "2 Sessions - 4 Spaces - 1 Agent");
}

TEST_CASE("server status surface keeps its log inside the runtime",
    "[server][status-surface][slice9]")
{
    const std::filesystem::path runtime
        = "D:/state/runtime/server-v1";
    CHECK(default_server_log_path(runtime)
        == runtime / "draxul-server.log");
}

TEST_CASE("server status surface formats live Session rows",
    "[server][status-surface][slice9]")
{
    const std::vector<ServerSessionStatusSnapshot> sessions{
        {
            .session_id = "default",
            .session_name = "Daily",
            .spaces = 2,
            .terminals = 3,
            .live_terminals = 2,
            .checkpoint_state = "saved",
        },
        {
            .session_id = "long-work-session",
            .session_name = "Long Work",
            .spaces = 1,
            .terminals = 12,
            .live_terminals = 0,
            .checkpoint_state = "restored",
        },
    };

    const std::string table
        = format_server_session_listing_table(sessions);
    const std::string expected
        = "SESSION ID         NAME       SPACES  TERMINALS  LIVE  CHECKPOINT\n"
          "-----------------  ---------  ------  ---------  ----  ----------\n"
          "default            Daily           2          3     2  saved\n"
          "long-work-session  Long Work       1         12     0  restored\n";
    CHECK(table == expected);
}
