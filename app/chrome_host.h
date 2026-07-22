#pragma once

#include "chrome_layout.h"
#include "chrome_text_layer.h"
#include "chrome_vector_pass.h"
#include "rename_editor.h"
#include "tab_controller.h"

#include <chrono>
#include <draxul/app_config_types.h>
#include <draxul/base_renderer.h>
#include <draxul/host.h>
#include <draxul/host_kind.h>
#include <draxul/renderer.h>
#include <draxul/system_resource_monitor.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace draxul
{

// ChromeHost is the central layout manager. It presents one or more tabs
// (each with its own SplitTree + hosts) and draws window chrome (pane dividers,
// focus indicator, future tab bar) using NanoVG.
class ChromeHost final : public IHost
{
public:
    // Shared dependencies passed to every tab's PaneManager.
    struct Deps
    {
        AppConfig* config = nullptr;
        IGridRenderer* grid_renderer = nullptr;
        TextService* text_service = nullptr;

        // Read-only tab info for tab bar / divider rendering.
        const TabController* tab_controller = nullptr;
        const SystemResourceSnapshot* system_resource_snapshot = nullptr;
        std::function<std::optional<std::pair<std::string, float>>()> chord_indicator = nullptr;
        // Weather callbacks — return emoji (for example "\u2600\uFE0F") and
        // temperature ("18°C") separately
        // for split-styled rendering. Both empty = no weather pill shown.
        std::function<std::string()> weather_emoji;
        std::function<std::string()> weather_temperature;

        // Apply a user-typed name to a tab. Owner (App) sets
        // tab.name and marks tab.name_user_set so subsequent OSC 7
        // updates don't overwrite the user's choice.
        std::function<void(int tab_id, std::string name)> set_tab_name;
        // Apply a user-typed name to a pane (per-leaf override). Owner (App)
        // forwards this to PaneManager::set_pane_name. Empty name clears the
        // override and reverts to host->status_text().
        std::function<void(LeafId leaf, std::string name)> set_pane_name;
        // Look up the existing user override for a pane. Returns empty string
        // when no override is set. Used to seed the rename buffer.
        std::function<std::string(LeafId leaf)> get_pane_name;
        // Request the host to schedule another frame even when no input
        // arrived (used to drive the rename caret blink).
        std::function<void()> request_frame;
    };

    explicit ChromeHost(Deps deps);

    // IHost overrides
    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const HostViewport& viewport) override;
    void pump() override {}
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    bool dispatch_action(std::string_view /*action*/) override
    {
        return false;
    }
    void request_close() override {}
    Color default_background() const override
    {
        return { 0, 0, 0, 0 };
    }
    HostRuntimeState runtime_state() const override
    {
        return { true };
    }
    HostDebugState debug_state() const override
    {
        return { "chrome" };
    }

    // Tab bar height in pixels. Remains visible even with a single tab.
    int tab_bar_height() const;

    // Hit-test a point (physical pixels) against the tab bar.
    // Returns the 1-based tab index if hit, or 0 if not in the tab bar.
    int hit_test_tab(int px, int py) const;

    // ----- Inline tab/pane rename (WI 128) -----------------------------
    // A single edit session can target either a tab or a pane
    // status pill. Only one target is active at a time; starting a new edit
    // commits any in-progress one.
    //
    // Tab target:
    void begin_tab_rename(int tab_index);
    // Begin editing the tab corresponding to a tab_id. Same semantics
    // as begin_tab_rename(int). Used by the rename_tab command palette
    // action which knows the active tab id, not its 1-based index.
    void begin_tab_rename_by_id(int tab_id);
    bool is_editing_tab() const;
    int editing_tab_id() const;
    // Pane target:
    void begin_pane_rename(LeafId leaf);
    bool is_editing_pane() const;
    LeafId editing_leaf_id() const;
    // True while any rename session (tab or pane) is in progress. Used by
    // InputDispatcher to route key/text events to the rename layer.
    bool is_editing() const;
    // Commit the current edit buffer (tab or pane) and exit edit mode.
    // No-op when not editing. Empty buffers do not overwrite the existing
    // name — they just exit edit mode.
    void commit_tab_rename();
    void cancel_tab_rename();
    // Forward a text input event to the active rename buffer. Returns true
    // if the event was consumed.
    bool on_rename_text_input(const std::string& utf8);
    // Forward a key event to the active rename buffer. Handles Enter /
    // Escape / Backspace / Delete / Left / Right / Home / End. Returns true
    // if the event was consumed.
    bool on_rename_key(int sdl_keycode);

    // Hit-test a point (physical pixels) against the per-pane status pills.
    // Returns the LeafId of the pane whose pill contains the point, or
    // kInvalidLeaf if none. Only valid after the first draw() call —
    // ChromeHost caches the pill rects from the most recent frame.
    LeafId hit_test_pane_status_pill(int px, int py) const;

    // Access the active tab's tree for divider/focus rendering.
    const SplitTree& active_tree() const;

private:
    ChromeLayoutInput build_layout_input() const;
    void apply_rename_commit(RenameCommit commit);
    const ChromeTheme& theme() const;

    // Resolve a tab id from a 1-based tab index, or -1 if out of range.
    int tab_id_for_index(int tab_index) const;

    Deps deps_;
    ChromeVectorPass vector_pass_;
    ChromeTextLayer text_layer_;
    HostViewport viewport_{};
    bool running_ = false;
    RenameEditor rename_editor_;
    mutable ChromeLayoutOutput last_layout_;
};

} // namespace draxul
