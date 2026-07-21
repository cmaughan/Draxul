#include "session_cli.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace draxul;

namespace
{

SessionCliServices fake_services()
{
    using AttachStatus = SessionAttachServer::AttachStatus;
    return {
        .list_sessions = [](std::string*) { return std::vector<SessionSummary>{}; },
        .try_attach = [](std::string_view, std::string*) { return AttachStatus::NoServer; },
        .send_command = [](std::string_view, SessionAttachServer::Command, std::string*) {
            return AttachStatus::NoServer;
        },
        .query_live_session = [](std::string_view, SessionAttachServer::LiveSessionInfo*, std::string*) {
            return false;
        },
        .rename_live_session = [](std::string_view, std::string_view, std::string*) { return false; },
        .rename_saved_session = [](std::string_view, std::string_view, std::string*) { return false; },
        .delete_saved_state = [](std::string_view, std::string*) { return false; },
        .delete_runtime_metadata = [](std::string_view, std::string*) { return false; },
        .now = [] { return std::chrono::steady_clock::time_point{}; },
        .sleep_for = [](std::chrono::milliseconds) {},
    };
}

} // namespace

TEST_CASE("session cli request selects one mode", "[session][cli]")
{
    struct Case
    {
        SessionCliMode expected;
        void (*select)(ParsedArgs&);
    };
    const Case cases[] = {
        { SessionCliMode::List, [](ParsedArgs& args) { args.list_sessions = true; } },
        { SessionCliMode::Attach, [](ParsedArgs& args) { args.attach_session = true; } },
        { SessionCliMode::Detach, [](ParsedArgs& args) { args.detach_session = true; } },
        { SessionCliMode::Rename, [](ParsedArgs& args) { args.rename_session = true; } },
        { SessionCliMode::Kill, [](ParsedArgs& args) { args.kill_session = true; } },
    };

    for (const auto& test : cases)
    {
        ParsedArgs args;
        args.session_id = "named";
        args.session_name = "Display Name";
        test.select(args);
        const auto request = SessionCliRequest::from_parsed_args(args);
        CHECK(request.mode == test.expected);
        CHECK(request.session_id == "named");
        CHECK(request.session_name == "Display Name");
    }
}

TEST_CASE("session cli request rejects conflicting modes", "[session][cli]")
{
    ParsedArgs args;
    args.attach_session = true;
    args.kill_session = true;
    const auto request = SessionCliRequest::from_parsed_args(args);
    REQUIRE(request.mode == SessionCliMode::Invalid);

    const auto result = SessionCli(fake_services()).run(request);
    CHECK(result.disposition == SessionCliDisposition::Error);
    CHECK(result.error == "error: multiple session CLI modes were requested\n");
}

TEST_CASE("session cli continue and list modes preserve output", "[session][cli]")
{
    auto services = fake_services();
    SessionCli cli(std::move(services));
    CHECK(cli.run({}).disposition == SessionCliDisposition::Continue);

    const auto result = cli.run({ .mode = SessionCliMode::List });
    CHECK(result.disposition == SessionCliDisposition::Handled);
    CHECK(result.output == "No saved sessions.\n");
}

TEST_CASE("session cli attach reports each protocol result", "[session][cli]")
{
    using AttachStatus = SessionAttachServer::AttachStatus;
    struct Case
    {
        AttachStatus status;
        const char* service_error;
        SessionCliDisposition disposition;
        const char* text;
    };
    const Case cases[] = {
        { AttachStatus::Attached, "", SessionCliDisposition::Handled, "Attached to session 'work'.\n" },
        { AttachStatus::NoServer, "", SessionCliDisposition::Error, "No running session 'work'.\n" },
        { AttachStatus::Error, "denied", SessionCliDisposition::Error,
            "Failed to attach to session 'work': denied\n" },
        { AttachStatus::Error, "", SessionCliDisposition::Error,
            "Failed to attach to session 'work': unknown error\n" },
    };

    for (const auto& test : cases)
    {
        auto services = fake_services();
        services.try_attach = [test](std::string_view, std::string* error) {
            if (error)
                *error = test.service_error;
            return test.status;
        };
        const auto result = SessionCli(std::move(services)).run(
            { .mode = SessionCliMode::Attach, .session_id = "work" });
        CHECK(result.disposition == test.disposition);
        CHECK((result.output.empty() ? result.error : result.output) == test.text);
    }
}

TEST_CASE("session cli detach waits until the owner confirms detachment", "[session][cli]")
{
    using AttachStatus = SessionAttachServer::AttachStatus;
    auto now = std::make_shared<std::chrono::steady_clock::time_point>();
    int queries = 0;
    auto services = fake_services();
    services.send_command = [](std::string_view id, SessionAttachServer::Command command, std::string*) {
        CHECK(id == "shell");
        CHECK(command == SessionAttachServer::Command::Detach);
        return AttachStatus::Attached;
    };
    services.query_live_session = [&queries](std::string_view, SessionAttachServer::LiveSessionInfo* info, std::string*) {
        info->detached = ++queries == 2;
        return true;
    };
    services.now = [now] { return *now; };
    services.sleep_for = [now](std::chrono::milliseconds duration) { *now += duration; };

    const auto result = SessionCli(std::move(services)).run(
        { .mode = SessionCliMode::Detach, .session_id = "shell" });
    CHECK(result.disposition == SessionCliDisposition::Handled);
    CHECK(result.output == "Detached session 'shell'.\n");
    CHECK(queries == 2);
}

TEST_CASE("session cli rename falls back to saved records", "[session][cli]")
{
    auto services = fake_services();
    services.rename_live_session = [](std::string_view, std::string_view, std::string* error) {
        *error = "not live";
        return false;
    };
    services.rename_saved_session = [](std::string_view id, std::string_view name, std::string*) {
        CHECK(id == "work");
        CHECK(name == "Renamed");
        return true;
    };
    const auto result = SessionCli(std::move(services)).run(
        { .mode = SessionCliMode::Rename, .session_id = "work", .session_name = "Renamed" });
    CHECK(result.disposition == SessionCliDisposition::Handled);
    CHECK(result.output == "Renamed saved session 'work' to 'Renamed'.\n");
}

TEST_CASE("session cli kill distinguishes running saved and missing sessions", "[session][cli]")
{
    using AttachStatus = SessionAttachServer::AttachStatus;
    struct Case
    {
        AttachStatus status;
        bool deleted;
        SessionCliDisposition disposition;
        const char* text;
    };
    const Case cases[] = {
        { AttachStatus::Attached, false, SessionCliDisposition::Handled, "Killed running session 'work'.\n" },
        { AttachStatus::NoServer, true, SessionCliDisposition::Handled, "Deleted saved session 'work'.\n" },
        { AttachStatus::NoServer, false, SessionCliDisposition::Error,
            "No running or saved session 'work'.\n" },
        { AttachStatus::Error, false, SessionCliDisposition::Error,
            "Failed to kill session 'work': protocol error\n" },
    };

    for (const auto& test : cases)
    {
        auto services = fake_services();
        services.send_command = [test](std::string_view, SessionAttachServer::Command, std::string* error) {
            if (error)
                *error = "protocol error";
            return test.status;
        };
        services.delete_saved_state = [test](std::string_view, std::string*) { return test.deleted; };
        const auto result = SessionCli(std::move(services)).run(
            { .mode = SessionCliMode::Kill, .session_id = "work" });
        CHECK(result.disposition == test.disposition);
        CHECK((result.output.empty() ? result.error : result.output) == test.text);
    }
}

TEST_CASE("session cli retry returns success and preserves the final error", "[session][cli]")
{
    using AttachStatus = SessionAttachServer::AttachStatus;
    SECTION("success")
    {
        auto now = std::make_shared<std::chrono::steady_clock::time_point>();
        int attempts = 0;
        auto services = fake_services();
        services.try_attach = [&attempts](std::string_view, std::string*) {
            return ++attempts == 3 ? AttachStatus::Attached : AttachStatus::NoServer;
        };
        services.now = [now] { return *now; };
        services.sleep_for = [now](std::chrono::milliseconds duration) { *now += duration; };
        CHECK(SessionCli(std::move(services)).try_attach_with_retry(
            "work", std::chrono::milliseconds(500), nullptr));
        CHECK(attempts == 3);
    }

    SECTION("timeout")
    {
        auto now = std::make_shared<std::chrono::steady_clock::time_point>();
        auto services = fake_services();
        services.try_attach = [](std::string_view, std::string* error) {
            *error = "still starting";
            return AttachStatus::NoServer;
        };
        services.now = [now] { return *now; };
        services.sleep_for = [now](std::chrono::milliseconds duration) { *now += duration; };
        std::string error;
        CHECK_FALSE(SessionCli(std::move(services)).try_attach_with_retry(
            "work", std::chrono::milliseconds(100), &error));
        CHECK(error == "still starting");
    }
}
