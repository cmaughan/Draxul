#include <catch2/catch_all.hpp>

#include "host_manager.h"

using namespace draxul;

TEST_CASE("host manager: split panes use the platform shell for non-shell primary hosts", "[host_manager]")
{
#ifdef _WIN32
    constexpr HostKind expected = HostKind::PowerShell;
#else
    constexpr HostKind expected = HostKind::Zsh;
#endif

    REQUIRE(HostManager::platform_default_split_host_kind() == expected);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Nvim) == expected);
    REQUIRE(HostManager::split_host_kind_for(HostKind::MegaCity) == expected);
}

TEST_CASE("host manager: split panes preserve explicit shell host choices", "[host_manager]")
{
    REQUIRE(HostManager::split_host_kind_for(HostKind::PowerShell) == HostKind::PowerShell);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Bash) == HostKind::Bash);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Zsh) == HostKind::Zsh);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Wsl) == HostKind::Wsl);
}
