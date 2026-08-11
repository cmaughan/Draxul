#include <catch2/catch_all.hpp>

#include "cli_args.h"
#include "command_palette.h"
#include "support/fake_host.h"

#include <draxul/host_registry.h>

#include <algorithm>

using namespace draxul;

namespace
{

HostFactory fake_provider()
{
    return [] { return std::make_unique<tests::FakeHost>("provider"); };
}

bool contains_action(const gui::PaletteViewState& state, std::string_view wanted)
{
    return std::any_of(state.entries.begin(), state.entries.end(), [&](const auto& entry) {
        return entry.name == wanted;
    });
}

ParseArgsResult parse_host(std::string_view name)
{
    return parse_args({ "draxul", "--host", std::string(name) });
}

} // namespace

TEST_CASE("host registry exposes ordered metadata and canonical aliases", "[host][registry]")
{
    HostProviderRegistry registry;
    registry.register_provider(HostKind::Nvim, fake_provider());
    registry.register_provider(HostKind::SatView, fake_provider());

    const auto providers = registry.available_providers();
    REQUIRE(providers.size() == 2);
    CHECK(providers[0].kind == HostKind::Nvim);
    CHECK(providers[0].canonical_cli_name == "nvim");
    CHECK(providers[1].kind == HostKind::SatView);
    CHECK(providers[1].canonical_cli_name == "satview");
    CHECK(registry.parse_available_kind("sat") == HostKind::SatView);
    CHECK_FALSE(registry.parse_available_kind("megacity").has_value());
    CHECK(registry.available_cli_names() == "nvim, satview");

    for (const auto& provider : providers)
    {
        CHECK(parse_host_kind(provider.canonical_cli_name) == provider.kind);
        CHECK(std::string(to_string(provider.kind)) == provider.canonical_cli_name);
        for (const auto& alias : provider.aliases)
            CHECK(parse_host_kind(alias) == provider.kind);
    }
}

TEST_CASE("command palette advertises only registered visible providers", "[host][palette]")
{
    HostProviderRegistry registry;
    registry.register_provider(HostKind::Nvim, fake_provider());
    registry.register_provider(HostKind::SatView, fake_provider());
    registry.register_provider(HostKind::NanoVGDemo, fake_provider());

    CommandPalette palette(CommandPalette::Deps{ .host_registry = &registry });
    palette.open();
    const auto state = palette.view_state(120, 40);

    CHECK(contains_action(state, "split_vertical nvim"));
    CHECK(contains_action(state, "split_horizontal satview"));
    CHECK(contains_action(state, "new_tab satview"));
    CHECK_FALSE(contains_action(state, "split_vertical megacity"));
    CHECK_FALSE(contains_action(state, "new_tab nanovg-demo"));
    CHECK_FALSE(registry.create(HostKind::MegaCity));

    const auto* nanovg = registry.metadata(HostKind::NanoVGDemo);
    REQUIRE(nanovg != nullptr);
    CHECK(nanovg->test_only);
    CHECK_FALSE(nanovg->palette_visible);
}

TEST_CASE("CLI rejects unknown and unavailable host providers but preserves aliases", "[host][cli]")
{
    CHECK(parse_host("definitely-not-a-host").error.has_value());

    HostProviderRegistry registry;
    registry.register_provider(HostKind::Nvim, fake_provider());

    const auto unavailable = parse_host("sat");
    REQUIRE_FALSE(unavailable.error.has_value());
    REQUIRE(unavailable.args.host_kind == HostKind::SatView);
    const auto availability_error = validate_host_provider_availability(unavailable.args, registry);
    REQUIRE(availability_error.has_value());
    CHECK(availability_error->find("satview") != std::string::npos);
    CHECK(availability_error->find("nvim") != std::string::npos);

    const auto available = parse_host("nvim");
    CHECK_FALSE(validate_host_provider_availability(available.args, registry).has_value());

    // Test-only providers are hidden from advertised choices but remain
    // launchable by the render harness when named explicitly.
    registry.register_provider(HostKind::NanoVGDemo, fake_provider());
    const auto nanovg = parse_host("nanovg-demo");
    CHECK_FALSE(validate_host_provider_availability(nanovg.args, registry).has_value());
    CHECK(registry.available_cli_names() == "nvim");
}

TEST_CASE("built-in host registration matches platform availability", "[host][registry][platform]")
{
    HostProviderRegistry registry;
    register_builtin_host_providers(registry);
    register_server_shell_host_metadata(registry);
    CHECK(registry.has(HostKind::Nvim));
    CHECK(registry.has(HostKind::Bash));
    CHECK(registry.has(HostKind::Zsh));
    CHECK_FALSE(registry.create(HostKind::Bash));
    CHECK_FALSE(registry.create(HostKind::Zsh));
#ifdef _WIN32
    CHECK(registry.has(HostKind::PowerShell));
    CHECK(registry.has(HostKind::Wsl));
    CHECK_FALSE(registry.create(HostKind::PowerShell));
    CHECK_FALSE(registry.create(HostKind::Wsl));
#else
    CHECK_FALSE(registry.has(HostKind::PowerShell));
    CHECK_FALSE(registry.has(HostKind::Wsl));
#endif
}

TEST_CASE("host metadata makes platform and optional-module availability explicit", "[host][registry][platform]")
{
    HostProviderRegistry registry;
    HostProviderMetadata windows_only{
        .kind = HostKind::PowerShell,
        .display_name = "PowerShell",
        .canonical_cli_name = "powershell",
        .platforms = { .windows = true, .macos = false, .other_unix = false },
        .launch_contexts = HostLaunchContext::Cli,
    };
    registry.register_provider(std::move(windows_only), fake_provider());
#ifdef _WIN32
    CHECK(registry.has(HostKind::PowerShell));
#else
    CHECK_FALSE(registry.has(HostKind::PowerShell));
#endif

    for (const bool megacity_enabled : { false, true })
    {
        for (const bool satview_enabled : { false, true })
        {
            HostProviderRegistry optional_registry;
            optional_registry.register_provider(HostKind::Nvim, fake_provider());
            if (megacity_enabled)
                optional_registry.register_provider(HostKind::MegaCity, fake_provider());
            if (satview_enabled)
                optional_registry.register_provider(HostKind::SatView, fake_provider());

            CommandPalette palette(CommandPalette::Deps{ .host_registry = &optional_registry });
            palette.open();
            const auto state = palette.view_state(120, 40);
            CHECK(contains_action(state, "new_tab megacity") == megacity_enabled);
            CHECK(contains_action(state, "new_tab satview") == satview_enabled);
            CHECK(optional_registry.has(HostKind::MegaCity) == megacity_enabled);
            CHECK(optional_registry.has(HostKind::SatView) == satview_enabled);
        }
    }
}
