#include <catch2/catch_test_macros.hpp>

#include "support/fake_host.h"
#include <draxul/host_registry.h>

using namespace draxul;

TEST_CASE("host API registry owns provider metadata and factories", "[host_api][registry]")
{
    HostProviderRegistry registry;
    registry.register_provider(HostKind::Kanban,
        [] { return std::make_unique<tests::FakeHost>("kanban"); });

    REQUIRE(registry.has(HostKind::Kanban));
    REQUIRE(registry.parse_available_kind("kb") == HostKind::Kanban);
    REQUIRE(registry.create(HostKind::Kanban) != nullptr);
    REQUIRE(registry.available_cli_names() == "kanban");

    registry.clear();
    CHECK_FALSE(registry.has(HostKind::Kanban));
}

TEST_CASE("host API registers server shells as metadata only", "[host_api][registry]")
{
    HostProviderRegistry registry;
    register_server_shell_host_metadata(registry);

    CHECK(registry.has(HostKind::Bash));
    CHECK(registry.has(HostKind::Zsh));
    CHECK_FALSE(registry.create(HostKind::Bash));
    CHECK_FALSE(registry.create(HostKind::Zsh));
}
