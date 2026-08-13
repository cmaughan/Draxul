#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

std::string read_source(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    return { std::istreambuf_iterator<char>(input), {} };
}

} // namespace

TEST_CASE("each app pump owns a bounded macOS autorelease pool",
    "[app][macos][lifetime]")
{
    const auto source = read_source(
        std::filesystem::path(DRAXUL_PROJECT_ROOT) / "app" / "app.cpp");
    REQUIRE_FALSE(source.empty());

    const auto pump = source.find("bool App::pump_once(");
    REQUIRE(pump != std::string::npos);
    const auto next_function = source.find("\nvoid App::pump_background_hosts()", pump);
    REQUIRE(next_function != std::string::npos);
    const auto body = source.substr(pump, next_function - pump);

    CHECK(body.find("ScopedAutoreleasePool autorelease_pool;")
        != std::string::npos);
}
