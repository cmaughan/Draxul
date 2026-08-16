#include <catch2/catch_all.hpp>

#include "support/fake_grid_host.h"
#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <draxul/app_config.h>
#include <draxul/app_options.h>
#include <draxul/text_service.h>
#include <draxul/unavailable_host.h>

#include "pane_manager.h"
#include "split_tree.h"
#include <draxul/grid_host_base.h>
#include <utility>

using namespace draxul;
using namespace draxul::tests;

namespace
{

class LifetimeTestHost final : public draxul::tests::FakeHost
{
public:
    using FakeHost::FakeHost;

    bool initialize(const HostContext& ctx, IHostCallbacks& callbacks) override
    {
        captured_launch = ctx.launch_options;
        return FakeHost::initialize(ctx, callbacks);
    }

    std::string display_name() const override
    {
        return stable_display_name;
    }

    HostLaunchOptions captured_launch;
    std::string stable_display_name;
};

// Alias for the shared FakeGridHost (replaces the ad-hoc GuardedGridHost).
using GuardedGridHost = draxul::tests::FakeGridHost;

class ExitableTestHost final : public draxul::tests::FakeHost
{
public:
    using FakeHost::FakeHost;

    void simulate_exit(int exit_code = 1)
    {
        running_ = false;
        exit_code_ = exit_code;
    }

    std::optional<int> exit_code() const override
    {
        return exit_code_;
    }

private:
    std::optional<int> exit_code_;
};

class LaunchCaptureHost final : public draxul::tests::FakeHost
{
public:
    using FakeHost::FakeHost;

    bool initialize(const HostContext& ctx, IHostCallbacks& callbacks) override
    {
        captured_launch = ctx.launch_options;
        return FakeHost::initialize(ctx, callbacks);
    }

    HostLaunchOptions captured_launch;
};

struct PaneManagerHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestHostCallbacks callbacks;
    AppOptions options;
    AppConfig config;
    float display_ppi = 96.0f;
    std::vector<LifetimeTestHost*> created_hosts;
    std::vector<std::shared_ptr<int>> shutdown_counters;
    int before_host_destroyed_calls = 0;
    bool fail_next_initialize = false;
    std::string next_init_error_code;
    std::optional<HostKind> unavailable_factory_kind;
    bool allow_local_layout_mutation = true;
    std::function<void(DividerId, float)>
        request_projected_divider_ratio;
    PaneManager manager;

    explicit PaneManagerHarness(
        bool allow_mutation = true,
        std::function<void(DividerId, float)> ratio_callback = {})
        : allow_local_layout_mutation(allow_mutation)
        , request_projected_divider_ratio(std::move(ratio_callback))
        , manager(make_deps())
    {
        draxul::tests::init_text_service(
            text_service, TextService::DEFAULT_POINT_SIZE, display_ppi);
    }

    PaneManager::Deps make_deps()
    {
        options.load_user_config = false;
        options.save_user_config = false;
        options.host_kind = HostKind::Nvim;
        options.host_factory = [this](HostKind kind) -> std::unique_ptr<IHost> {
            if (unavailable_factory_kind == kind)
                return nullptr;
            auto counter = std::make_shared<int>(0);
            auto host = std::make_unique<LifetimeTestHost>("lifetime-test");
            host->fail_initialize
                = std::exchange(fail_next_initialize, false);
            host->init_error_code_message
                = std::exchange(next_init_error_code, {});
            host->on_shutdown_callback = [counter] { ++(*counter); };
            shutdown_counters.push_back(counter);
            created_hosts.push_back(host.get());
            return host;
        };

        PaneManager::Deps deps;
        deps.options = &options;
        deps.config = &config;
        deps.window = &window;
        deps.grid_renderer = &renderer;
        deps.text_service = &text_service;
        deps.display_ppi = &display_ppi;
        deps.allow_local_layout_mutation
            = allow_local_layout_mutation;
        deps.request_projected_divider_ratio
            = request_projected_divider_ratio;
        deps.before_host_destroyed = [this](IHost*) {
            ++before_host_destroyed_calls;
        };
        deps.compute_viewport = [](const PaneDescriptor& desc) {
            HostViewport viewport;
            viewport.pixel_pos = desc.pixel_pos;
            viewport.pixel_size = desc.pixel_size;
            viewport.grid_size = { 80, 24 };
            return viewport;
        };
        return deps;
    }
};

struct ShellPaneManagerHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestHostCallbacks callbacks;
    AppOptions options;
    AppConfig config;
    float display_ppi = 96.0f;
    std::vector<ExitableTestHost*> created_hosts;
    PaneManager manager;

    ShellPaneManagerHarness()
        : manager(make_deps())
    {
        draxul::tests::init_text_service(
            text_service, TextService::DEFAULT_POINT_SIZE, display_ppi);
    }

    PaneManager::Deps make_deps()
    {
        options.load_user_config = false;
        options.save_user_config = false;
        options.host_kind = HostKind::PowerShell;
        options.host_factory = [this](HostKind) -> std::unique_ptr<IHost> {
            auto host = std::make_unique<ExitableTestHost>("shell-test");
            created_hosts.push_back(host.get());
            return host;
        };

        PaneManager::Deps deps;
        deps.options = &options;
        deps.config = &config;
        deps.window = &window;
        deps.grid_renderer = &renderer;
        deps.text_service = &text_service;
        deps.display_ppi = &display_ppi;
        deps.compute_viewport = [](const PaneDescriptor& desc) {
            HostViewport viewport;
            viewport.pixel_pos = desc.pixel_pos;
            viewport.pixel_size = desc.pixel_size;
            viewport.grid_size = { 80, 24 };
            return viewport;
        };
        return deps;
    }
};

} // namespace

TEST_CASE("pane manager: split panes use the platform shell for non-shell primary hosts", "[pane_manager]")
{
#ifdef _WIN32
    constexpr HostKind expected = HostKind::PowerShell;
#else
    constexpr HostKind expected = HostKind::Zsh;
#endif

    REQUIRE(PaneManager::platform_default_split_host_kind() == expected);
    REQUIRE(PaneManager::split_host_kind_for(HostKind::Nvim) == expected);
    REQUIRE(PaneManager::split_host_kind_for(HostKind::Plugin) == expected);
}

TEST_CASE("pane manager: split panes preserve explicit shell host choices", "[pane_manager]")
{
    REQUIRE(PaneManager::split_host_kind_for(HostKind::PowerShell) == HostKind::PowerShell);
    REQUIRE(PaneManager::split_host_kind_for(HostKind::Bash) == HostKind::Bash);
    REQUIRE(PaneManager::split_host_kind_for(HostKind::Zsh) == HostKind::Zsh);
    REQUIRE(PaneManager::split_host_kind_for(HostKind::Wsl) == HostKind::Wsl);
}

TEST_CASE("pane manager: explicit primary host override does not inherit incompatible source options",
    "[pane_manager]")
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);

    TestHostCallbacks callbacks;
    AppConfig config;
    AppOptions options;
    options.host_kind = HostKind::Markdown;
    options.host_source_path = "notes/example.md";
    options.host_command = "custom-command";
    options.host_args = { "--custom" };
    options.startup_commands = { "echo inherited" };
    options.host_working_dir = "D:/work";

    std::vector<LaunchCaptureHost*> created_hosts;
    options.host_factory = [&created_hosts](HostKind) -> std::unique_ptr<IHost> {
        auto host = std::make_unique<LaunchCaptureHost>("launch-capture");
        created_hosts.push_back(host.get());
        return host;
    };

    PaneManager::Deps deps;
    deps.options = &options;
    deps.config = &config;
    deps.window = &window;
    deps.grid_renderer = &renderer;
    deps.text_service = &text_service;
    deps.compute_viewport = [](const PaneDescriptor& desc) {
        HostViewport viewport;
        viewport.pixel_pos = desc.pixel_pos;
        viewport.pixel_size = desc.pixel_size;
        viewport.grid_size = { 80, 24 };
        return viewport;
    };

    PaneManager manager(deps);
    REQUIRE(manager.create(callbacks, 800, 600, HostKind::Kanban));
    REQUIRE(created_hosts.size() == 1);

    const HostLaunchOptions& launch = created_hosts.front()->captured_launch;
    REQUIRE(launch.kind == HostKind::Kanban);
    REQUIRE(launch.source_path.empty());
    REQUIRE(launch.command.empty());
    REQUIRE(launch.args.empty());
    REQUIRE(launch.startup_commands.empty());
    REQUIRE(launch.working_dir == "D:/work");
}

TEST_CASE("pane manager: shell launch options inherit configured scrollback capacity",
    "[pane_manager][scrollback]")
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    draxul::tests::init_text_service(text_service);

    TestHostCallbacks callbacks;
    AppConfig config;
    config.scrollback_lines = 77;
    AppOptions options;
    options.host_kind = HostKind::Zsh;
    options.load_user_config = false;
    options.save_user_config = false;

    std::vector<LaunchCaptureHost*> created_hosts;
    options.host_factory = [&created_hosts](HostKind) -> std::unique_ptr<IHost> {
        auto host = std::make_unique<LaunchCaptureHost>("launch-capture");
        created_hosts.push_back(host.get());
        return host;
    };

    PaneManager::Deps deps;
    deps.options = &options;
    deps.config = &config;
    deps.window = &window;
    deps.grid_renderer = &renderer;
    deps.text_service = &text_service;
    deps.compute_viewport = [](const PaneDescriptor& desc) {
        HostViewport viewport;
        viewport.pixel_pos = desc.pixel_pos;
        viewport.pixel_size = desc.pixel_size;
        viewport.grid_size = { 80, 24 };
        return viewport;
    };

    PaneManager manager(deps);
    REQUIRE(manager.create(callbacks, 800, 600));
    REQUIRE(created_hosts.size() == 1);
    REQUIRE(created_hosts.front()->captured_launch.scrollback_lines == 77);

    REQUIRE(manager.split_focused(SplitDirection::Vertical, HostKind::Zsh, callbacks) != kInvalidLeaf);
    REQUIRE(created_hosts.size() == 2);
    REQUIRE(created_hosts.back()->captured_launch.scrollback_lines == 77);
}

// --- SplitTree-level lifecycle tests ---
// These test the SplitTree that PaneManager uses for layout and lifecycle.

TEST_CASE("split tree: reset creates single leaf", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    REQUIRE(root != kInvalidLeaf);
    REQUIRE(tree.leaf_count() == 1);
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: split creates two leaves with correct viewports", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Vertical);

    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(tree.leaf_count() == 2);

    PaneDescriptor d1 = tree.descriptor_for(root);
    PaneDescriptor d2 = tree.descriptor_for(new_leaf);

    // Both panes should have non-zero dimensions
    REQUIRE(d1.pixel_size.x > 0);
    REQUIRE(d1.pixel_size.y > 0);
    REQUIRE(d2.pixel_size.x > 0);
    REQUIRE(d2.pixel_size.y > 0);

    // Combined widths should approximately equal total (minus divider)
    INFO("vertical split divides width between panes");
    REQUIRE(d1.pixel_size.x + d2.pixel_size.x + SplitTree::kDividerWidth <= 800);
}

TEST_CASE("split tree: horizontal split divides height", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Horizontal);

    REQUIRE(new_leaf != kInvalidLeaf);

    PaneDescriptor d1 = tree.descriptor_for(root);
    PaneDescriptor d2 = tree.descriptor_for(new_leaf);

    INFO("horizontal split divides height between panes");
    REQUIRE(d1.pixel_size.y + d2.pixel_size.y + SplitTree::kDividerWidth <= 600);
    INFO("horizontal split preserves full width for both panes");
    REQUIRE(d1.pixel_size.x == 800);
    REQUIRE(d2.pixel_size.x == 800);
}

TEST_CASE("split tree: close leaf collapses tree and reassigns focus", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Vertical);

    tree.set_focused(new_leaf);
    REQUIRE(tree.focused() == new_leaf);

    bool closed = tree.close_leaf(new_leaf);
    REQUIRE(closed);
    REQUIRE(tree.leaf_count() == 1);

    // Focus should be reassigned to the remaining leaf
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: cannot close last leaf", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);

    bool closed = tree.close_leaf(root);
    REQUIRE_FALSE(closed);
    REQUIRE(tree.leaf_count() == 1);
}

TEST_CASE("split tree: hit test returns correct leaf", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId right = tree.split_leaf(root, SplitDirection::Vertical);

    PaneDescriptor d_left = tree.descriptor_for(root);
    PaneDescriptor d_right = tree.descriptor_for(right);

    // Hit test in the left pane
    auto result_left = tree.hit_test(d_left.pixel_pos.x + 10, d_left.pixel_pos.y + 10);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result_left));
    REQUIRE(std::get<SplitTree::LeafHit>(result_left).id == root);

    // Hit test in the right pane
    auto result_right = tree.hit_test(d_right.pixel_pos.x + 10, d_right.pixel_pos.y + 10);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result_right));
    REQUIRE(std::get<SplitTree::LeafHit>(result_right).id == right);
}

TEST_CASE("split tree: recompute updates all descriptors proportionally", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId right = tree.split_leaf(root, SplitDirection::Vertical);

    PaneDescriptor d1_before = tree.descriptor_for(root);

    // Resize the window to 1600x1200
    tree.recompute(1600, 1200);

    PaneDescriptor d1_after = tree.descriptor_for(root);
    PaneDescriptor d2_after = tree.descriptor_for(right);

    INFO("recomputed pane is wider than before");
    REQUIRE(d1_after.pixel_size.x > d1_before.pixel_size.x);
    INFO("recomputed panes fit in new window dimensions");
    REQUIRE(d1_after.pixel_size.x + d2_after.pixel_size.x + SplitTree::kDividerWidth <= 1600);
    INFO("height adjusts to new window height");
    REQUIRE(d1_after.pixel_size.y == 1200);
}

TEST_CASE("split tree: for_each_leaf visits all leaves", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);
    LeafId third = tree.split_leaf(second, SplitDirection::Horizontal);

    std::vector<LeafId> visited;
    tree.for_each_leaf([&](LeafId id, const PaneDescriptor&) {
        visited.push_back(id);
    });

    REQUIRE(visited.size() == 3);
    // All three IDs should be present
    REQUIRE(std::find(visited.begin(), visited.end(), root) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), second) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), third) != visited.end());
}

TEST_CASE("split tree: focus changes via set_focused", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);

    REQUIRE(tree.focused() == root);

    tree.set_focused(second);
    REQUIRE(tree.focused() == second);

    tree.set_focused(root);
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: double split creates three panes", "[pane_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);
    LeafId third = tree.split_leaf(root, SplitDirection::Horizontal);

    REQUIRE(tree.leaf_count() == 3);

    // All descriptors should have non-zero dimensions
    REQUIRE(tree.descriptor_for(root).pixel_size.x > 0);
    REQUIRE(tree.descriptor_for(second).pixel_size.x > 0);
    REQUIRE(tree.descriptor_for(third).pixel_size.x > 0);
}

TEST_CASE("pane manager: callbacks remain valid across pane teardown", "[pane_manager]")
{
    PaneManagerHarness harness;

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.created_hosts.size() == 1);

    const LeafId new_leaf = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(harness.created_hosts.size() == 2);

    auto* primary = harness.created_hosts[0];
    auto* secondary = harness.created_hosts[1];
    REQUIRE(primary != nullptr);
    REQUIRE(secondary != nullptr);

    secondary->fire_callback_on_shutdown = true;
    REQUIRE(harness.manager.close_leaf(new_leaf));
    REQUIRE(harness.callbacks.request_frame_calls == 1);
    REQUIRE(*harness.shutdown_counters[1] == 1);

    primary->trigger_frame_request();
    REQUIRE(harness.callbacks.request_frame_calls == 2);
}

TEST_CASE("pane manager: markdown preview splits below the owner and keeps focus", "[pane_manager]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId owner = harness.manager.focused_leaf();
    REQUIRE(harness.manager.host_count() == 1);
    REQUIRE_FALSE(harness.manager.has_markdown_preview());

    const LeafId preview = harness.manager.show_markdown_preview(
        owner, 2.0f / 3.0f, "/tmp/card.md", harness.callbacks);
    REQUIRE(preview != kInvalidLeaf);
    REQUIRE(preview != owner);
    REQUIRE(harness.manager.has_markdown_preview());
    REQUIRE(harness.manager.host_count() == 2);

    // Focus stays on the board, not the freshly-created preview pane.
    CHECK(harness.manager.focused_leaf() == owner);

    // The preview sits below the owner and takes roughly the bottom third.
    const PaneDescriptor owner_desc = harness.manager.tree().descriptor_for(owner);
    const PaneDescriptor preview_desc = harness.manager.tree().descriptor_for(preview);
    CHECK(preview_desc.pixel_pos.y > owner_desc.pixel_pos.y);
    CHECK(owner_desc.pixel_size.y > preview_desc.pixel_size.y);
    CHECK(preview_desc.pixel_size.y >= 150);
    CHECK(preview_desc.pixel_size.y <= 240);
}

TEST_CASE("pane manager: markdown preview reuses the pane and reloads on refresh", "[pane_manager]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId owner = harness.manager.focused_leaf();

    // First call creates the pane; the initial source arrives via the host's
    // launch options (loaded on init), so it is not a dispatch_action.
    const LeafId first
        = harness.manager.show_markdown_preview(owner, 2.0f / 3.0f, "/tmp/a.md", harness.callbacks);
    // Second call must reuse the same pane and reload it via dispatch_action.
    const LeafId second
        = harness.manager.show_markdown_preview(owner, 2.0f / 3.0f, "/tmp/b.md", harness.callbacks);

    CHECK(first == second);
    CHECK(harness.manager.host_count() == 2);
    REQUIRE(harness.created_hosts.size() == 2);
    const auto& actions = harness.created_hosts[1]->dispatched_actions;
    CHECK(std::ranges::find(actions, std::string("open_file:/tmp/b.md")) != actions.end());
}

TEST_CASE("pane manager: hiding the markdown preview restores the owner", "[pane_manager]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId owner = harness.manager.focused_leaf();

    harness.manager.show_markdown_preview(owner, 2.0f / 3.0f, "/tmp/card.md", harness.callbacks);
    REQUIRE(harness.manager.host_count() == 2);

    harness.manager.hide_markdown_preview();
    CHECK_FALSE(harness.manager.has_markdown_preview());
    CHECK(harness.manager.host_count() == 1);
    CHECK(harness.manager.focused_leaf() == owner);

    // Hiding again is a harmless no-op.
    harness.manager.hide_markdown_preview();
    CHECK(harness.manager.host_count() == 1);
}

TEST_CASE("pane manager: closing the preview pane clears preview tracking", "[pane_manager]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId owner = harness.manager.focused_leaf();

    const LeafId preview
        = harness.manager.show_markdown_preview(owner, 2.0f / 3.0f, "/tmp/card.md", harness.callbacks);
    REQUIRE(harness.manager.has_markdown_preview());

    // Closing the preview by any other path must not leave a dangling ref.
    REQUIRE(harness.manager.close_leaf(preview));
    CHECK_FALSE(harness.manager.has_markdown_preview());
}

TEST_CASE("pane manager: pane labels prefer custom then stable host identity",
    "[pane_manager][chrome][label]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId leaf = harness.manager.focused_leaf();

    CHECK(harness.manager.pane_display_name(leaf) == "Neovim");
    REQUIRE(harness.created_hosts.size() == 1);
    harness.created_hosts.front()->stable_display_name = "PowerShell";
    CHECK(harness.manager.pane_display_name(leaf) == "PowerShell");

    harness.manager.set_pane_name(leaf, "build");
    CHECK(harness.manager.pane_display_name(leaf) == "build");
    harness.manager.set_pane_name(leaf, {});
    CHECK(harness.manager.pane_display_name(leaf) == "PowerShell");
}

TEST_CASE("pane manager: layout snapshot round-trips pane metadata", "[pane_manager]")
{
    PaneManagerHarness harness;

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId root = harness.manager.focused_leaf();

    HostLaunchOptions launch;
    launch.kind = HostKind::PowerShell;
    launch.command = "codex";
    launch.args = { "--model", "gpt-5" };
    launch.working_dir = "D:/dev/Draxul";

    const LeafId split = harness.manager.split_focused(
        SplitDirection::Vertical, std::move(launch), harness.callbacks);
    REQUIRE(split != kInvalidLeaf);

    harness.manager.set_pane_name(root, "left");
    harness.manager.set_pane_name(split, "right");
    harness.manager.set_agent_identity(split, {
                                                  .profile_id = "codex",
                                                  .kind = "codex",
                                                  .display_name = "Codex",
                                                  .instance_id = "agent-4-7-pane-right",
                                              });
    REQUIRE(harness.manager.set_agent_session_ref(split, {
                                                             .source = "draxul:codex",
                                                             .agent_kind = "codex",
                                                             .integration_version = 1,
                                                             .sequence = 7,
                                                             .kind = AgentSessionRefKind::Id,
                                                             .value = "codex-native-session",
                                                         }));
    harness.manager.set_focused(split);
    harness.manager.toggle_zoom(800, 600);

    auto saved = harness.manager.snapshot_layout();
    REQUIRE(saved);
    REQUIRE(saved->panes.size() == 2);
    CHECK(!saved->panes[0].pane_id.empty());
    CHECK(!saved->panes[1].pane_id.empty());
    CHECK(saved->panes[0].pane_id != saved->panes[1].pane_id);

    PaneManagerHarness restored_harness;
    restored_harness.config.agents_resume_on_restore = true;
    REQUIRE(restored_harness.manager.restore_layout(
        restored_harness.callbacks, 800, 600, *saved));
    REQUIRE(restored_harness.manager.host_count() == 2);
    REQUIRE(restored_harness.created_hosts.size() == 2);
    CHECK(restored_harness.created_hosts[1]->captured_launch.command == "codex");
    CHECK(restored_harness.created_hosts[1]->captured_launch.args
        == (std::vector<std::string>{ "resume", "codex-native-session" }));
    CHECK(std::find(restored_harness.created_hosts[1]->captured_launch.environment.begin(),
              restored_harness.created_hosts[1]->captured_launch.environment.end(),
              std::pair<std::string, std::string>{
                  "DRAXUL_AGENT_INSTANCE_ID", "agent-4-7-pane-right" })
        != restored_harness.created_hosts[1]->captured_launch.environment.end());
    CHECK(restored_harness.manager.focused_leaf() == split);
    CHECK(restored_harness.manager.pane_name(root) == "left");
    CHECK(restored_harness.manager.pane_name(split) == "right");
    REQUIRE(restored_harness.manager.agent_identity(split) != nullptr);
    CHECK(restored_harness.manager.agent_identity(split)->kind == "codex");
    CHECK(restored_harness.manager.agent_identity(split)->display_name == "Codex");
    CHECK(restored_harness.manager.agent_identity(split)->instance_id
        == "agent-4-7-pane-right");
    CHECK(restored_harness.manager.is_zoomed());
    CHECK(restored_harness.manager.zoomed_leaf() == split);

    auto restored = restored_harness.manager.snapshot_layout();
    REQUIRE(restored);
    REQUIRE(restored->panes.size() == 2);

    const auto restored_split = std::find_if(restored->panes.begin(), restored->panes.end(),
        [split](const PaneManager::PaneSnapshot& pane) { return pane.leaf_id == split; });
    REQUIRE(restored_split != restored->panes.end());
    const auto original_split = std::find_if(saved->panes.begin(), saved->panes.end(),
        [split](const PaneManager::PaneSnapshot& pane) { return pane.leaf_id == split; });
    REQUIRE(original_split != saved->panes.end());
    CHECK(restored_split->pane_id == original_split->pane_id);
    CHECK(restored_split->launch.kind == HostKind::PowerShell);
    CHECK(restored_split->launch.command == "codex");
    CHECK(restored_split->launch.args
        == (std::vector<std::string>{ "--model", "gpt-5" }));
    CHECK(restored_split->launch.working_dir == "D:/dev/Draxul");
    CHECK(restored_split->launch.startup_commands.empty());
    REQUIRE(restored_split->agent);
    CHECK(restored_split->agent->instance_id == "agent-4-7-pane-right");
    REQUIRE(restored_split->agent_session);
    CHECK(restored_split->agent_session->value == "codex-native-session");
}

TEST_CASE("pane manager: projected layout preserves unchanged live hosts",
    "[pane_manager][topology]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.created_hosts.size() == 1);
    LifetimeTestHost* original_host = harness.created_hosts.front();
    const auto original_shutdown = harness.shutdown_counters.front();

    SplitTree split_tree;
    const LeafId root = split_tree.reset(800, 600);
    const LeafId added
        = split_tree.split_leaf(root, SplitDirection::Vertical);
    REQUIRE(added != kInvalidLeaf);
    split_tree.set_focused(added);

    PaneManager::PaneLayoutSnapshot projected;
    projected.tree = split_tree.snapshot();
    projected.panes = {
        {
            .leaf_id = root,
            .launch = { .kind = HostKind::Nvim },
            .pane_name = "kept",
            .pane_id = "shared-pane-1",
        },
        {
            .leaf_id = added,
            .launch = { .kind = HostKind::PowerShell },
            .pane_name = "new",
            .pane_id = "shared-pane-2",
        },
    };
    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    REQUIRE(harness.created_hosts.size() == 2);
    CHECK(harness.manager.host_for(root) == original_host);
    CHECK(*original_shutdown == 0);
    CHECK(harness.manager.pane_name(root) == "kept");
    CHECK(harness.manager.pane_id(added) == "shared-pane-2");

    projected.tree.root->ratio = 0.7f;
    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    CHECK(harness.created_hosts.size() == 2);
    CHECK(harness.manager.host_for(root) == original_host);
    CHECK(*original_shutdown == 0);
    CHECK(harness.before_host_destroyed_calls == 0);

    SplitTree single_tree;
    REQUIRE(single_tree.reset(800, 600) == root);
    PaneManager::PaneLayoutSnapshot collapsed;
    collapsed.tree = single_tree.snapshot();
    collapsed.panes = { projected.panes.front() };
    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, collapsed));
    CHECK(harness.manager.host_count() == 1);
    CHECK(harness.manager.host_for(root) == original_host);
    CHECK(*original_shutdown == 0);
    REQUIRE(harness.shutdown_counters.size() == 2);
    CHECK(*harness.shutdown_counters[1] == 1);
    CHECK(harness.before_host_destroyed_calls == 1);
}

TEST_CASE("pane manager: projected host preserves typed initialization failure",
    "[pane_manager][topology]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));

    SplitTree split_tree;
    const LeafId root = split_tree.reset(800, 600);
    const LeafId added
        = split_tree.split_leaf(root, SplitDirection::Vertical);
    REQUIRE(added != kInvalidLeaf);
    PaneManager::PaneLayoutSnapshot projected;
    projected.tree = split_tree.snapshot();
    projected.panes = {
        {
            .leaf_id = root,
            .launch = { .kind = HostKind::Nvim },
            .pane_id = "shared-pane-1",
        },
        {
            .leaf_id = added,
            .launch = {
                .kind = HostKind::RemoteTerminal,
                .remote_terminal_id = "already-removed",
            },
            .pane_id = "shared-pane-2",
        },
    };
    harness.fail_next_initialize = true;
    harness.next_init_error_code = "terminal_not_found";

    CHECK_FALSE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    CHECK(harness.manager.error_code() == "terminal_not_found");
    CHECK(harness.manager.host_count() == 1);
}

TEST_CASE("pane manager: projected unavailable client host keeps the rest of the layout live",
    "[pane_manager][topology][unavailable]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(
        harness.callbacks, 800, 600));

    SplitTree split_tree;
    const LeafId root = split_tree.reset(800, 600);
    const LeafId added
        = split_tree.split_leaf(
            root, SplitDirection::Vertical);
    REQUIRE(added != kInvalidLeaf);

    PaneManager::PaneLayoutSnapshot projected;
    projected.tree = split_tree.snapshot();
    projected.panes = {
        {
            .leaf_id = root,
            .launch = { .kind = HostKind::Nvim },
            .pane_id = "shared-pane-1",
        },
        {
            .leaf_id = added,
            .launch = {
                .kind = HostKind::PowerShell,
                .client_host_kind = "future_view",
            },
            .pane_id = "shared-pane-2",
        },
    };

    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    CHECK(harness.manager.host_count() == 2);
    CHECK(harness.manager.host_for(root) != nullptr);
    const auto* unavailable = dynamic_cast<UnavailableHost*>(
        harness.manager.host_for(added));
    REQUIRE(unavailable != nullptr);
    CHECK(unavailable->status_text()
        == "future_view not available in this build");

    // A known optional kind absent from this build degrades in exactly the
    // same way; ordinary local launches still retain their failure behavior.
    harness.unavailable_factory_kind = HostKind::Plugin;
    projected.panes[1].launch.kind = HostKind::Plugin;
    projected.panes[1].launch.client_host_kind = "plugin";
    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    unavailable = dynamic_cast<UnavailableHost*>(
        harness.manager.host_for(added));
    REQUIRE(unavailable != nullptr);
    CHECK(unavailable->status_text()
        == "plugin not available in this build");
}

TEST_CASE("pane manager: identifies only server-owned remote terminals",
    "[pane_manager][topology]")
{
    PaneManagerHarness harness(false);
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));

    SplitTree split_tree;
    const LeafId root = split_tree.reset(800, 600);
    const LeafId remote
        = split_tree.split_leaf(root, SplitDirection::Vertical);
    REQUIRE(remote != kInvalidLeaf);

    PaneManager::PaneLayoutSnapshot projected;
    projected.tree = split_tree.snapshot();
    projected.panes = {
        {
            .leaf_id = root,
            .launch = { .kind = HostKind::PowerShell },
            .pane_id = "shared-pane-1",
        },
        {
            .leaf_id = remote,
            .launch = {
                .kind = HostKind::RemoteTerminal,
                .remote_terminal_id = "terminal-2",
            },
            .pane_id = "shared-pane-2",
        },
    };
    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));

    CHECK_FALSE(harness.manager
            .is_server_owned_remote_terminal_leaf(root));
    CHECK(harness.manager
            .is_server_owned_remote_terminal_leaf(remote));

    PaneManagerHarness local_harness;
    REQUIRE(local_harness.manager.create(
        local_harness.callbacks, 800, 600,
        HostKind::RemoteTerminal));
    CHECK_FALSE(local_harness.manager
            .is_server_owned_remote_terminal_leaf(
                local_harness.manager.focused_leaf()));
}

TEST_CASE("pane manager restores and refreshes a projected companion preview",
    "[pane_manager][topology][preview]")
{
    PaneManagerHarness harness(false);
    REQUIRE(harness.manager.create(
        harness.callbacks, 800, 600));

    SplitTree split_tree;
    const LeafId owner = split_tree.reset(800, 600);
    const LeafId preview
        = split_tree.split_leaf(
            owner, SplitDirection::Horizontal);
    REQUIRE(preview != kInvalidLeaf);
    auto tree_snapshot = split_tree.snapshot();
    REQUIRE(tree_snapshot.root);
    tree_snapshot.root->ratio = 2.0f / 3.0f;

    PaneManager::PaneLayoutSnapshot projected;
    projected.tree = std::move(tree_snapshot);
    projected.panes = {
        {
            .leaf_id = owner,
            .launch = {
                .kind = HostKind::Kanban,
                .client_host_kind = "kanban",
            },
            .pane_id = "kanban-pane",
        },
        {
            .leaf_id = preview,
            .launch = {
                .kind = HostKind::Markdown,
                .source_path = "D:/cards/one.md",
                .client_host_kind = "markdown",
                .companion_owner_pane_id = "kanban-pane",
            },
            .pane_id = "preview-pane",
        },
    };

    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    CHECK(harness.manager.has_markdown_preview());
    CHECK(harness.manager.markdown_preview_leaf() == preview);
    const size_t hosts_after_restore
        = harness.created_hosts.size();
    auto* preview_host = dynamic_cast<LifetimeTestHost*>(
        harness.manager.host_for(preview));
    REQUIRE(preview_host);

    projected.panes[1].launch.source_path
        = "D:/cards/two.md";
    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    CHECK(harness.created_hosts.size()
        == hosts_after_restore);
    REQUIRE(preview_host->dispatched_actions.size() == 1);
    CHECK(preview_host->dispatched_actions.back()
        == "open_file:D:/cards/two.md");
}

TEST_CASE("pane manager: projected divider drag previews and reports locally",
    "[pane_manager][topology]")
{
    std::optional<std::pair<DividerId, float>> requested;
    PaneManagerHarness harness(false,
        [&](DividerId id, float ratio) {
            requested = { id, ratio };
        });
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));

    SplitTree split_tree;
    const LeafId root = split_tree.reset(800, 600);
    const LeafId added
        = split_tree.split_leaf(root, SplitDirection::Vertical);
    PaneManager::PaneLayoutSnapshot projected;
    projected.tree = split_tree.snapshot();
    projected.panes = {
        {
            .leaf_id = root,
            .launch = { .kind = HostKind::Nvim },
            .pane_id = "shared-pane-1",
        },
        {
            .leaf_id = added,
            .launch = { .kind = HostKind::PowerShell },
            .pane_id = "shared-pane-2",
        },
    };
    REQUIRE(harness.manager.reconcile_projected_layout(
        harness.callbacks, 800, 600, projected));
    REQUIRE(harness.manager.divider_ratio(0)
        == Catch::Approx(0.5f));

    harness.manager.update_divider_from_pixel(
        0, 600, 300, 0, 0);
    REQUIRE(requested);
    CHECK(requested->first == 0);
    CHECK(requested->second > 0.7f);
    CHECK(harness.manager.divider_ratio(0)
        == Catch::Approx(requested->second));
}

TEST_CASE("pane manager: only terminal shell layouts are checkpoint-restorable",
    "[pane_manager][session]")
{
    PaneManagerHarness product_harness;
    REQUIRE(product_harness.manager.create(product_harness.callbacks, 800, 600));
    CHECK_FALSE(product_harness.manager.has_restorable_shell_session());

    ShellPaneManagerHarness shell_harness;
    REQUIRE(shell_harness.manager.create(shell_harness.callbacks, 800, 600));
    CHECK(shell_harness.manager.has_restorable_shell_session());
}

TEST_CASE("pane manager: dead shell panes are preserved for restart", "[pane_manager]")
{
    ShellPaneManagerHarness harness;

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.created_hosts.size() == 1);

    const LeafId root = harness.manager.focused_leaf();
    auto* root_host = harness.created_hosts.front();
    REQUIRE(root_host != nullptr);
    REQUIRE_FALSE(harness.manager.should_preserve_dead_leaf(root));

    root_host->simulate_exit();
    CHECK(harness.manager.should_preserve_dead_leaf(root));
}

TEST_CASE("pane manager: clean shell exits are not preserved", "[pane_manager]")
{
    ShellPaneManagerHarness harness;

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.created_hosts.size() == 1);

    const LeafId root = harness.manager.focused_leaf();
    auto* root_host = harness.created_hosts.front();
    REQUIRE(root_host != nullptr);

    root_host->simulate_exit(0);
    CHECK_FALSE(harness.manager.should_preserve_dead_leaf(root));
}

TEST_CASE("pane manager: dead nvim panes are preserved for restart", "[pane_manager]")
{
    PaneManagerHarness harness;

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.created_hosts.size() == 1);

    const LeafId root = harness.manager.focused_leaf();
    auto* root_host = harness.created_hosts.front();
    REQUIRE(root_host != nullptr);
    REQUIRE_FALSE(harness.manager.should_preserve_dead_leaf(root));

    root_host->request_close();
    CHECK(harness.manager.should_preserve_dead_leaf(root));
}

TEST_CASE("grid host base: invalidated owner lifetime blocks renderer and callback use", "[pane_manager]")
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TestHostCallbacks callbacks;
    TextService text_service;
    draxul::tests::init_text_service(text_service);

    auto owner_lifetime = std::make_shared<int>(0);
    HostViewport viewport;
    viewport.pixel_size = { 640, 480 };
    viewport.grid_size = { 80, 24 };

    GuardedGridHost host;
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = viewport,
        .owner_lifetime = owner_lifetime,
    };
    REQUIRE(host.initialize(context, callbacks));

    const int baseline_set_cell_size_calls = renderer.set_cell_size_calls;
    owner_lifetime.reset();

    REQUIRE_NOTHROW(host.on_font_metrics_changed());
    REQUIRE(renderer.set_cell_size_calls == baseline_set_cell_size_calls);

    callbacks.request_frame_calls = 0;
    REQUIRE_NOTHROW(host.exercise_apply_grid_size(100, 40));
    REQUIRE(callbacks.request_frame_calls == 0);

    callbacks.last_text_input_area = { 7, 8, 9, 10 };
    REQUIRE_NOTHROW(host.set_viewport(viewport));
    REQUIRE(callbacks.last_text_input_area.x == 7);
    REQUIRE(callbacks.last_text_input_area.y == 8);
    REQUIRE(callbacks.last_text_input_area.w == 9);
    REQUIRE(callbacks.last_text_input_area.h == 10);

    text_service.shutdown();
}

// ---------------------------------------------------------------------------
// Pane zoom + close interaction tests (WI 65)
// ---------------------------------------------------------------------------
//
// Pin down toggle_zoom() / close_leaf() interactions:
//   * close the zoomed pane → zoom state cleared, surviving pane fills viewport
//   * close a non-zoomed pane in a 3-pane tree → still zoomed, viewport intact
//   * close in a 2-pane tree (whether zoomed pane or not) → zoom auto-cleared
//   * toggle zoom on/off → original layout restored
//   * single-pane toggle is a no-op (no zoom state set)
//   * focus navigation references the surviving pane after a zoomed-pane close

TEST_CASE("zoom: toggle on/off restores original two-pane layout", "[pane_manager][zoom]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(right != kInvalidLeaf);

    const PaneDescriptor before_left = harness.manager.tree().descriptor_for(left);
    const PaneDescriptor before_right = harness.manager.tree().descriptor_for(right);

    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);

    harness.manager.toggle_zoom(800, 600);
    REQUIRE_FALSE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == kInvalidLeaf);

    // Layout fully restored.
    const PaneDescriptor after_left = harness.manager.tree().descriptor_for(left);
    const PaneDescriptor after_right = harness.manager.tree().descriptor_for(right);
    CHECK(after_left.pixel_size.x == before_left.pixel_size.x);
    CHECK(after_left.pixel_size.y == before_left.pixel_size.y);
    CHECK(after_right.pixel_size.x == before_right.pixel_size.x);
    CHECK(after_right.pixel_size.y == before_right.pixel_size.y);
}

TEST_CASE("zoom: closing the zoomed pane clears zoom state", "[pane_manager][zoom]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    const LeafId third = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    REQUIRE(third != kInvalidLeaf);
    REQUIRE(harness.manager.host_count() == 3);

    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);

    REQUIRE(harness.manager.close_leaf(right));
    REQUIRE_FALSE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == kInvalidLeaf);
    REQUIRE(harness.manager.host_count() == 2);

    // Surviving panes have valid viewports.
    CHECK(harness.manager.tree().descriptor_for(left).pixel_size.x > 0);
    CHECK(harness.manager.tree().descriptor_for(third).pixel_size.x > 0);
}

TEST_CASE("zoom: closing a non-zoomed pane in a 3-pane tree leaves the other zoomed",
    "[pane_manager][zoom]")
{
    // close_leaf() inspects leaf_count() *before* the close: with 3 panes pre-close
    // (leaf_count()==3 > 2), the auto-cancel branch does not fire, so zoom survives.
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    const LeafId third = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    REQUIRE(harness.manager.host_count() == 3);

    // Zoom the right pane, then close a different pane (third).
    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);

    REQUIRE(harness.manager.close_leaf(third));
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);
    REQUIRE(harness.manager.host_count() == 2);
    CHECK(harness.manager.tree().descriptor_for(left).pixel_size.x > 0);
    CHECK(harness.manager.tree().descriptor_for(right).pixel_size.x > 0);
}

TEST_CASE("zoom: closing the last sibling in a 2-pane tree auto-clears zoom",
    "[pane_manager][zoom]")
{
    // With 2 panes pre-close, leaf_count()==2 <= 2 trips the defensive cancel,
    // even when the closed pane is NOT the zoomed one.
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(harness.manager.host_count() == 2);

    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());

    REQUIRE(harness.manager.close_leaf(left));
    REQUIRE_FALSE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.host_count() == 1);
}

TEST_CASE("zoom: closing a non-zoomed pane in a 4-pane tree preserves zoom",
    "[pane_manager][zoom]")
{
    // Same as the previous test but with a fourth pane so the close does NOT
    // trip the "drop to 2 panes" auto-cancel — verifies the zoomed pane stays
    // zoomed when there is still ambient layout to preserve.
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId a = harness.manager.focused_leaf();
    const LeafId b = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    harness.manager.set_focused(a);
    const LeafId c = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    harness.manager.set_focused(b);
    const LeafId d = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    REQUIRE(harness.manager.host_count() == 4);

    // Zoom pane B, then close pane D (a different, non-zoomed pane).
    harness.manager.set_focused(b);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == b);

    REQUIRE(harness.manager.close_leaf(d));
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == b);
    REQUIRE(harness.manager.host_count() == 3);
    CHECK(harness.manager.tree().descriptor_for(c).pixel_size.x > 0);
}

TEST_CASE("zoom: toggle is a no-op on a single-pane tree", "[pane_manager][zoom]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.manager.host_count() == 1);

    harness.manager.toggle_zoom(800, 600);
    CHECK_FALSE(harness.manager.is_zoomed());
    CHECK(harness.manager.zoomed_leaf() == kInvalidLeaf);
}

TEST_CASE("zoom: focus navigation after zoomed-pane close lands on a valid host",
    "[pane_manager][zoom]")
{
    PaneManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);

    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.close_leaf(right));

    // The surviving leaf must be focused and reachable through focused_host().
    REQUIRE(harness.manager.focused_leaf() == left);
    REQUIRE(harness.manager.focused_host() != nullptr);
    REQUIRE(harness.manager.host_for(left) == harness.manager.focused_host());
}
