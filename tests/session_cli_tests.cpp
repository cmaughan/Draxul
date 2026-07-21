#include "session_cli.h"

#include <catch2/catch_test_macros.hpp>

using namespace draxul;

namespace
{

SessionCliServices fake_services()
{
    return {
        .list_sessions = [](std::string*) { return std::vector<SessionSummary>{}; },
        .rename_saved_session = [](std::string_view, std::string_view, std::string*) { return false; },
        .delete_saved_state = [](std::string_view, std::string*) { return false; },
    };
}

} // namespace

TEST_CASE("session cli request selects one file-backed mode", "[session][cli]")
{
    struct Case
    {
        SessionCliMode expected;
        void (*select)(ParsedArgs&);
    };
    const Case cases[] = {
        { SessionCliMode::List, [](ParsedArgs& args) { args.list_sessions = true; } },
        { SessionCliMode::Rename, [](ParsedArgs& args) { args.rename_session = true; } },
        { SessionCliMode::Delete, [](ParsedArgs& args) { args.delete_session = true; } },
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
    args.rename_session = true;
    args.delete_session = true;
    const auto request = SessionCliRequest::from_parsed_args(args);
    REQUIRE(request.mode == SessionCliMode::Invalid);

    const auto result = SessionCli(fake_services()).run(request);
    CHECK(result.disposition == SessionCliDisposition::Error);
    CHECK(result.error == "error: multiple session CLI modes were requested\n");
}

TEST_CASE("session cli continue and list modes preserve output", "[session][cli]")
{
    SessionCli cli(fake_services());
    CHECK(cli.run({}).disposition == SessionCliDisposition::Continue);

    const auto result = cli.run({ .mode = SessionCliMode::List });
    CHECK(result.disposition == SessionCliDisposition::Handled);
    CHECK(result.output == "No saved sessions.\n");
}

TEST_CASE("session cli renames a saved session", "[session][cli]")
{
    auto services = fake_services();
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

TEST_CASE("session cli deletes saved sessions and reports missing records", "[session][cli]")
{
    SECTION("deleted")
    {
        auto services = fake_services();
        services.delete_saved_state = [](std::string_view id, std::string*) {
            CHECK(id == "work");
            return true;
        };
        const auto result = SessionCli(std::move(services)).run(
            { .mode = SessionCliMode::Delete, .session_id = "work" });
        CHECK(result.disposition == SessionCliDisposition::Handled);
        CHECK(result.output == "Deleted saved session 'work'.\n");
    }

    SECTION("missing")
    {
        const auto result = SessionCli(fake_services()).run(
            { .mode = SessionCliMode::Delete, .session_id = "work" });
        CHECK(result.disposition == SessionCliDisposition::Error);
        CHECK(result.error == "Failed to delete saved session 'work': session not found\n");
    }
}
