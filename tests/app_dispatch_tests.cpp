// app_dispatch_tests.cpp
//
// WI 122 — Mixed-host dispatch tests.
//
// Proves that App::dispatch_to_nvim_host() targets an NvimHost pane via the
// typed IHost::is_nvim_host() capability query rather than the old debug-name
// string heuristic (see WI 116). The rename scenario is a regression guard:
// changing the reported debug-state name must not affect dispatch routing.
//
// These tests wire a real App with fake subsystems and a host factory that
// returns "nvim-capable" or "terminal-capable" fake hosts based on HostKind.
// Actions are dispatched by calling the IHostCallbacks pointer that each fake
// host captures during initialize(); that callback pointer is the App itself
// (App privately implements IHostCallbacks), so the test exercises the real
// App::dispatch_to_nvim_host() implementation end-to-end.

#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

// ---------------------------------------------------------------------------
// IHost stub built on top of the shared FakeHost. Adds capability flags
// and method-style accessors for the existing test
// call sites without duplicating all the IHost plumbing.
// ---------------------------------------------------------------------------
class DispatchTrackingHost final : public tests::FakeHost
{
public:
    DispatchTrackingHost(bool is_nvim_flag, bool is_markdown_flag, std::string debug_name)
        : FakeHost(std::move(debug_name))
        , is_markdown_(is_markdown_flag)
    {
        is_nvim = is_nvim_flag;
    }

    bool is_markdown_host() const override
    {
        return is_markdown_;
    }

    // Method-style accessor wrapping the base vector member.
    const std::vector<std::string>& dispatched_actions() const
    {
        return FakeHost::dispatched_actions;
    }
    void clear_dispatched_actions()
    {
        FakeHost::dispatched_actions.clear();
    }

private:
    bool is_markdown_ = false;
};

// ---------------------------------------------------------------------------
// Registry of created hosts, keyed by HostKind so the factory can hand each
// kind its own fake. The factory is installed per-test via make_app_options().
// ---------------------------------------------------------------------------
struct DispatchHostRegistry
{
    std::vector<DispatchTrackingHost*> nvim_hosts;
    std::vector<DispatchTrackingHost*> markdown_hosts;
    std::vector<DispatchTrackingHost*> terminal_hosts;

    std::unique_ptr<IHost> make_host(HostKind kind)
    {
        if (kind == HostKind::Nvim)
        {
            auto host = std::make_unique<DispatchTrackingHost>(
                /*is_nvim=*/true, /*is_markdown=*/false, "nvim");
            nvim_hosts.push_back(host.get());
            return host;
        }
        if (kind == HostKind::Markdown)
        {
            auto host = std::make_unique<DispatchTrackingHost>(
                /*is_nvim=*/false, /*is_markdown=*/true, "markdown");
            markdown_hosts.push_back(host.get());
            return host;
        }
        auto host = std::make_unique<DispatchTrackingHost>(
            /*is_nvim=*/false, /*is_markdown=*/false, "terminal");
        terminal_hosts.push_back(host.get());
        return host;
    }
};

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

AppOptions make_app_options(DispatchHostRegistry& registry, HostKind primary_kind)
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    opts.override_display_ppi = 96.0f;
    opts.config_overrides.font_path = draxul::tests::bundled_font_path().string();
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.host_factory = [&registry](HostKind kind) -> std::unique_ptr<IHost> {
        return registry.make_host(kind);
    };
    opts.host_kind = primary_kind;
    return opts;
}

} // namespace

// ---------------------------------------------------------------------------
// WI 122 — case 1: primary pane is Neovim. Dispatching an nvim-targeted
// action reaches the NvimHost pane (and, with only one pane present, no
// new pane is created).
// ---------------------------------------------------------------------------

TEST_CASE("app dispatch: nvim-targeted action reaches single NvimHost pane",
    "[app_dispatch][WI122]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    DispatchHostRegistry registry;
    App app(make_app_options(registry, HostKind::Nvim));
    REQUIRE(app.initialize());

    REQUIRE(registry.nvim_hosts.size() == 1);
    REQUIRE(registry.terminal_hosts.empty());

    DispatchTrackingHost* nvim = registry.nvim_hosts.front();
    REQUIRE(nvim->callbacks() != nullptr);

    const bool ok = nvim->callbacks()->dispatch_to_nvim_host("open_file_at_type:main.cpp:123");
    REQUIRE(ok);

    // Single-pane scenario: the action must land on the existing nvim host,
    // and no additional host (nvim or otherwise) should have been spawned.
    REQUIRE(registry.nvim_hosts.size() == 1);
    REQUIRE(registry.terminal_hosts.empty());
    REQUIRE(nvim->dispatched_actions().size() == 1);
    REQUIRE(nvim->dispatched_actions().front() == "open_file_at_type:main.cpp:123");

    app.shutdown();
}

// ---------------------------------------------------------------------------
// WI 122 — case 2: tab has both a Neovim pane and a terminal pane.
// Dispatching an nvim-targeted action via the terminal pane's callbacks
// must still reach the NvimHost — routing is by capability, not focus or
// identity of the caller.
// ---------------------------------------------------------------------------

TEST_CASE("app dispatch: mixed-host tab routes nvim action to NvimHost pane",
    "[app_dispatch][WI122]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    // A user config with a simple single-key split_vertical binding so we
    // can drive a split without chord bookkeeping.
    TempDir temp("draxul-app-dispatch-mixed");
    HomeDirRedirect redir(temp.path);
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "[keybindings]\n"
               "split_vertical = \"Ctrl+Alt+V\"\n";
    }

    DispatchHostRegistry registry;
    FakeWindow* created_window = nullptr;

    AppOptions opts = make_app_options(registry, HostKind::Nvim);
    opts.load_user_config = true;
    opts.window_factory = [&created_window]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    REQUIRE(registry.nvim_hosts.size() == 1);
    REQUIRE(registry.terminal_hosts.empty());

    // Trigger a vertical split. The split defaults to the platform shell host
    // kind, so our factory returns a terminal-capable fake for the new pane.
    REQUIRE(created_window->on_key != nullptr);
    created_window->on_key(KeyEvent{ 0, SDLK_V, kModCtrl | kModAlt, true });

    REQUIRE(registry.nvim_hosts.size() == 1);
    REQUIRE(registry.terminal_hosts.size() == 1);

    DispatchTrackingHost* nvim = registry.nvim_hosts.front();
    DispatchTrackingHost* term = registry.terminal_hosts.front();
    REQUIRE(nvim->callbacks() != nullptr);
    REQUIRE(term->callbacks() != nullptr);

    // Dispatch via the terminal pane's callbacks — the shared App object is
    // the single IHostCallbacks implementation so both pointers alias it,
    // but we exercise the terminal side explicitly to document intent.
    const bool ok = term->callbacks()->dispatch_to_nvim_host("open_file:/tmp/from-terminal.txt");
    REQUIRE(ok);

    // Routing must land on the nvim host; the terminal host must not see the
    // nvim-targeted action.
    REQUIRE(nvim->dispatched_actions().size() == 1);
    REQUIRE(nvim->dispatched_actions().front() == "open_file:/tmp/from-terminal.txt");
    REQUIRE(term->dispatched_actions().empty());

    // No additional hosts were spawned: the existing NvimHost was reused.
    REQUIRE(registry.nvim_hosts.size() == 1);
    REQUIRE(registry.terminal_hosts.size() == 1);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// WI 122 — case 3: regression guard for WI 116. Rename the NvimHost's debug
// state to something other than "nvim". The capability-based lookup must
// still find it because routing keys off IHost::is_nvim_host(), not the
// debug-state name.
// ---------------------------------------------------------------------------

TEST_CASE("app dispatch: debug-name rename does not break nvim routing",
    "[app_dispatch][WI122][regression]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    DispatchHostRegistry registry;
    App app(make_app_options(registry, HostKind::Nvim));
    REQUIRE(app.initialize());
    REQUIRE(registry.nvim_hosts.size() == 1);

    DispatchTrackingHost* nvim = registry.nvim_hosts.front();
    REQUIRE(nvim->callbacks() != nullptr);

    // Mimic a host whose debug-state name disagrees with its actual capability.
    // Under the old debug-string heuristic (WI 116) this dispatch would have
    // failed to find the nvim pane and spawned a fresh one; under the typed
    // is_nvim_host() query it still routes correctly.
    nvim->set_debug_name("notanvim");

    const bool ok = nvim->callbacks()->dispatch_to_nvim_host("open_file_at_type:renamed.cpp:1");
    REQUIRE(ok);

    REQUIRE(registry.nvim_hosts.size() == 1);
    REQUIRE(registry.terminal_hosts.empty());
    REQUIRE(nvim->dispatched_actions().size() == 1);
    REQUIRE(nvim->dispatched_actions().front() == "open_file_at_type:renamed.cpp:1");

    app.shutdown();
}

// ---------------------------------------------------------------------------
// WI 122 — case 4: no Neovim pane present. The tab's primary host is
// a terminal; dispatching an nvim-targeted action must not crash and must
// lazily create a new NvimHost via a split (the current documented policy
// of App::dispatch_to_nvim_host()).
// ---------------------------------------------------------------------------

TEST_CASE("app dispatch: no-nvim tab spawns a new NvimHost on dispatch",
    "[app_dispatch][WI122]")
{
    const std::string font = draxul::tests::bundled_font_path().string();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    DispatchHostRegistry registry;
#ifdef _WIN32
    const HostKind primary = HostKind::PowerShell;
#else
    const HostKind primary = HostKind::Zsh;
#endif
    App app(make_app_options(registry, primary));
    REQUIRE(app.initialize());
    REQUIRE(registry.nvim_hosts.empty());
    REQUIRE(registry.terminal_hosts.size() == 1);

    DispatchTrackingHost* term = registry.terminal_hosts.front();
    REQUIRE(term->callbacks() != nullptr);

    const bool ok = term->callbacks()->dispatch_to_nvim_host("open_file:/tmp/spawn.txt");
    REQUIRE(ok);

    // A new NvimHost should have been created and the action delivered to it.
    REQUIRE(registry.nvim_hosts.size() == 1);
    REQUIRE(registry.terminal_hosts.size() == 1);

    DispatchTrackingHost* nvim = registry.nvim_hosts.front();
    REQUIRE(nvim->dispatched_actions().size() == 1);
    REQUIRE(nvim->dispatched_actions().front() == "open_file:/tmp/spawn.txt");

    // The pre-existing terminal host must not have received the nvim action.
    REQUIRE(term->dispatched_actions().empty());

    app.shutdown();
}
