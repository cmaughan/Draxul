#include <catch2/catch_all.hpp>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <draxul/app_config.h>
#include <draxul/text_service.h>

#include "host_manager.h"
#include "split_tree.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

class LifetimeTestHost final : public IHost
{
public:
    explicit LifetimeTestHost(std::shared_ptr<int> shutdown_calls)
        : shutdown_calls_(std::move(shutdown_calls))
    {
    }

    bool initialize(const HostContext&, IHostCallbacks& callbacks) override
    {
        callbacks_ = &callbacks;
        return true;
    }

    void shutdown() override
    {
        if (shutdown_calls_)
            ++(*shutdown_calls_);
        if (fire_callback_on_shutdown_ && callbacks_)
            callbacks_->request_frame();
        running_ = false;
    }

    bool is_running() const override
    {
        return running_;
    }

    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const HostViewport&) override {}
    void on_font_metrics_changed() override {}
    void pump() override {}
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override
    {
        running_ = false;
    }

    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    HostRuntimeState runtime_state() const override
    {
        HostRuntimeState state;
        state.content_ready = true;
        return state;
    }

    HostDebugState debug_state() const override
    {
        HostDebugState state;
        state.name = "lifetime-test";
        return state;
    }

    void trigger_frame_request() const
    {
        REQUIRE(callbacks_ != nullptr);
        callbacks_->request_frame();
    }

    void set_fire_callback_on_shutdown(bool enabled)
    {
        fire_callback_on_shutdown_ = enabled;
    }

    int shutdown_calls() const
    {
        return shutdown_calls_ ? *shutdown_calls_ : 0;
    }

private:
    IHostCallbacks* callbacks_ = nullptr;
    std::shared_ptr<int> shutdown_calls_;
    bool running_ = true;
    bool fire_callback_on_shutdown_ = false;
};

struct HostManagerHarness
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
    HostManager manager;

    HostManagerHarness()
        : manager(make_deps())
    {
        TextServiceConfig ts_cfg;
        ts_cfg.font_path = bundled_font_path();
        const bool ok = text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, display_ppi);
        REQUIRE(ok);
    }

    HostManager::Deps make_deps()
    {
        options.load_user_config = false;
        options.save_user_config = false;
        options.host_kind = HostKind::Nvim;
        options.host_factory = [this](HostKind) -> std::unique_ptr<IHost> {
            auto shutdown_calls = std::make_shared<int>(0);
            shutdown_counters.push_back(shutdown_calls);
            auto host = std::make_unique<LifetimeTestHost>(std::move(shutdown_calls));
            created_hosts.push_back(host.get());
            return host;
        };

        HostManager::Deps deps;
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

// ---------------------------------------------------------------------------
// Fake I3DHost for testing attach_3d_renderer() dispatch.
// ---------------------------------------------------------------------------
class Fake3DTestHost final : public I3DHost
{
public:
    bool initialize(const HostContext&, IHostCallbacks& callbacks) override
    {
        callbacks_ = &callbacks;
        return true;
    }

    void shutdown() override
    {
        running_ = false;
    }

    bool is_running() const override
    {
        return running_;
    }

    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const HostViewport&) override {}
    void on_font_metrics_changed() override {}
    void pump() override {}
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override
    {
        running_ = false;
    }

    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    HostRuntimeState runtime_state() const override
    {
        HostRuntimeState state;
        state.content_ready = true;
        return state;
    }

    HostDebugState debug_state() const override
    {
        HostDebugState state;
        state.name = "fake-3d";
        return state;
    }

    // I3DHost interface
    void attach_3d_renderer(I3DRenderer&) override
    {
        ++attach_calls;
    }

    void detach_3d_renderer() override {}

    int attach_calls = 0;

private:
    IHostCallbacks* callbacks_ = nullptr;
    bool running_ = true;
};

// A harness variant that lets us control which host type the factory produces.
struct I3DHarnessData
{
    std::vector<bool> host_sequence; // true = I3DHost, false = plain IHost
    size_t next_index = 0;
    std::vector<IHost*> created_hosts;
};

struct I3DHostManagerHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestHostCallbacks callbacks;
    AppOptions options;
    AppConfig config;
    float display_ppi = 96.0f;
    I3DHarnessData data;
    HostManager manager;

    explicit I3DHostManagerHarness(std::vector<bool> sequence)
        : manager(make_deps(std::move(sequence)))
    {
        TextServiceConfig ts_cfg;
        ts_cfg.font_path = bundled_font_path();
        const bool ok = text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, display_ppi);
        REQUIRE(ok);
    }

    HostManager::Deps make_deps(std::vector<bool> sequence)
    {
        data.host_sequence = std::move(sequence);
        options.load_user_config = false;
        options.save_user_config = false;
        options.host_kind = HostKind::Nvim;
        options.host_factory = [this](HostKind) -> std::unique_ptr<IHost> {
            bool make_3d = false;
            if (data.next_index < data.host_sequence.size())
                make_3d = data.host_sequence[data.next_index];
            ++data.next_index;

            if (make_3d)
            {
                auto host = std::make_unique<Fake3DTestHost>();
                data.created_hosts.push_back(host.get());
                return host;
            }
            else
            {
                auto counter = std::make_shared<int>(0);
                auto host = std::make_unique<LifetimeTestHost>(std::move(counter));
                data.created_hosts.push_back(host.get());
                return host;
            }
        };

        HostManager::Deps deps;
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

// --- SplitTree-level lifecycle tests ---
// These test the SplitTree that HostManager uses for layout and lifecycle.

TEST_CASE("split tree: reset creates single leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    REQUIRE(root != kInvalidLeaf);
    REQUIRE(tree.leaf_count() == 1);
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: split creates two leaves with correct viewports", "[host_manager]")
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

TEST_CASE("split tree: horizontal split divides height", "[host_manager]")
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

TEST_CASE("split tree: close leaf collapses tree and reassigns focus", "[host_manager]")
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

TEST_CASE("split tree: cannot close last leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);

    bool closed = tree.close_leaf(root);
    REQUIRE_FALSE(closed);
    REQUIRE(tree.leaf_count() == 1);
}

TEST_CASE("split tree: hit test returns correct leaf", "[host_manager]")
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

TEST_CASE("split tree: recompute updates all descriptors proportionally", "[host_manager]")
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

TEST_CASE("split tree: for_each_leaf visits all leaves", "[host_manager]")
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

TEST_CASE("split tree: focus changes via set_focused", "[host_manager]")
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

TEST_CASE("split tree: double split creates three panes", "[host_manager]")
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

TEST_CASE("host manager: callbacks remain valid across pane teardown", "[host_manager]")
{
    HostManagerHarness harness;

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.created_hosts.size() == 1);

    const LeafId new_leaf = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(harness.created_hosts.size() == 2);

    auto* primary = harness.created_hosts[0];
    auto* secondary = harness.created_hosts[1];
    REQUIRE(primary != nullptr);
    REQUIRE(secondary != nullptr);

    secondary->set_fire_callback_on_shutdown(true);
    REQUIRE(harness.manager.close_leaf(new_leaf));
    REQUIRE(harness.callbacks.request_frame_calls == 1);
    REQUIRE(*harness.shutdown_counters[1] == 1);

    primary->trigger_frame_request();
    REQUIRE(harness.callbacks.request_frame_calls == 2);
}

// ---------------------------------------------------------------------------
// I3DHost capability tests — validates the dynamic_cast dispatch in
// HostManager::create_host_for_leaf.
// ---------------------------------------------------------------------------

TEST_CASE("host manager: I3DHost receives attach_3d_renderer exactly once", "[host_manager][i3dhost]")
{
    I3DHostManagerHarness harness({ true });

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.data.created_hosts.size() == 1);

    auto* host_3d = dynamic_cast<Fake3DTestHost*>(harness.data.created_hosts[0]);
    REQUIRE(host_3d != nullptr);
    REQUIRE(host_3d->attach_calls == 1);
}

TEST_CASE("host manager: plain IHost does not receive attach_3d_renderer", "[host_manager][i3dhost]")
{
    I3DHostManagerHarness harness({ false });

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.data.created_hosts.size() == 1);

    auto* host_3d = dynamic_cast<Fake3DTestHost*>(harness.data.created_hosts[0]);
    REQUIRE(host_3d == nullptr);
}

TEST_CASE("host manager: mixed registration selectively attaches 3D renderer", "[host_manager][i3dhost]")
{
    I3DHostManagerHarness harness({ true, false });

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));

    const LeafId new_leaf = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(harness.data.created_hosts.size() == 2);

    auto* host_3d = dynamic_cast<Fake3DTestHost*>(harness.data.created_hosts[0]);
    REQUIRE(host_3d != nullptr);
    REQUIRE(host_3d->attach_calls == 1);

    auto* host_plain = dynamic_cast<Fake3DTestHost*>(harness.data.created_hosts[1]);
    REQUIRE(host_plain == nullptr);
}

TEST_CASE("host manager: I3DHost added after plain host still receives attachment", "[host_manager][i3dhost]")
{
    I3DHostManagerHarness harness({ false, true });

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));

    const LeafId new_leaf = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(harness.data.created_hosts.size() == 2);

    auto* host_plain = dynamic_cast<Fake3DTestHost*>(harness.data.created_hosts[0]);
    REQUIRE(host_plain == nullptr);

    auto* host_3d = dynamic_cast<Fake3DTestHost*>(harness.data.created_hosts[1]);
    REQUIRE(host_3d != nullptr);
    REQUIRE(host_3d->attach_calls == 1);
}
