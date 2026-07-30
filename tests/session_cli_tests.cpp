#include "session_cli.h"

#include <catch2/catch_test_macros.hpp>

using namespace draxul;

namespace
{

SessionCliServices fake_services()
{
    return {
        .rename_saved_session = [](std::string_view, std::string_view, std::string*) { return false; },
    };
}

} // namespace

TEST_CASE("session cli request selects the file-backed rename mode", "[session][cli]")
{
    ParsedArgs args;
    args.rename_session = true;
    args.session_id = "named";
    args.session_name = "Display Name";
    const auto request = SessionCliRequest::from_parsed_args(args);
    CHECK(request.mode == SessionCliMode::Rename);
    CHECK(request.session_id == "named");
    CHECK(request.session_name == "Display Name");
}

TEST_CASE("session cli continue mode leaves startup untouched", "[session][cli]")
{
    SessionCli cli(fake_services());
    CHECK(cli.run({}).disposition == SessionCliDisposition::Continue);
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
