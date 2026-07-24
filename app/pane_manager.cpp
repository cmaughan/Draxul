#include "pane_manager.h"

#include <draxul/app_config.h>
#include <draxul/app_options.h>
#include <draxul/base_renderer.h>
#include <draxul/grid_host_base.h>
#include <draxul/host_kind.h>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>

#include <charconv>

namespace draxul
{

namespace
{

bool is_terminal_shell_host(HostKind kind)
{
    using enum HostKind;
    switch (kind)
    {
    case PowerShell:
    case Bash:
    case Zsh:
    case Wsl:
        return true;
    case Nvim:
    case MegaCity:
    case BioView:
    case SatView:
    case NanoVGDemo:
    case Markdown:
    case Kanban:
    case Score:
        return false;
    }
    return false;
}

HostKind platform_default_split_host_kind_impl()
{
#ifdef _WIN32
    return HostKind::PowerShell;
#else
    return HostKind::Zsh;
#endif
}

void apply_terminal_config(HostLaunchOptions& launch, const AppConfig& config)
{
    if (!config.terminal.fg.empty())
        launch.terminal_fg = parse_hex_color(config.terminal.fg);
    if (!config.terminal.bg.empty())
        launch.terminal_bg = parse_hex_color(config.terminal.bg);
    launch.selection_max_cells = config.terminal.selection_max_cells;
    launch.copy_on_select = config.terminal.copy_on_select;
    launch.paste_confirm_lines = config.terminal.paste_confirm_lines;
    launch.url_detection = config.terminal.url_detection;
    launch.enable_osc8_hyperlinks = config.terminal.enable_osc8_hyperlinks;
    launch.enable_shell_integration_marks = config.terminal.enable_shell_integration_marks;
    launch.scrollback_lines = config.scrollback_lines;
}

void apply_global_host_options(HostLaunchOptions& launch, const AppOptions& options)
{
    launch.request_continuous_refresh = options.request_continuous_refresh;
    launch.show_host_ui_panels = !options.hide_host_ui_panels;
    launch.pty_capture_file = options.pty_capture_file;
}

PaneManager::SavedLaunchOptions save_launch_options(const HostLaunchOptions& launch)
{
    PaneManager::SavedLaunchOptions saved;
    saved.kind = launch.kind;
    saved.command = launch.command;
    saved.args = launch.args;
    saved.working_dir = launch.working_dir;
    saved.source_path = launch.source_path;
    saved.startup_commands = launch.startup_commands;
    saved.pty_capture_file = launch.pty_capture_file;
    return saved;
}

HostLaunchOptions restore_launch_options(const PaneManager::SavedLaunchOptions& saved,
    const PaneManager::Deps& deps)
{
    HostLaunchOptions launch;
    launch.kind = saved.kind;
    launch.command = saved.command;
    launch.args = saved.args;
    launch.working_dir = saved.working_dir;
    launch.source_path = saved.source_path;
    launch.startup_commands = saved.startup_commands;
    launch.pty_capture_file = saved.pty_capture_file;
    launch.enable_ligatures = deps.config ? deps.config->enable_ligatures : true;
    if (deps.config)
        apply_terminal_config(launch, *deps.config);
    if (deps.options)
        apply_global_host_options(launch, *deps.options);
    if (launch.working_dir.empty())
        launch.working_dir = deps.default_working_dir.empty() && deps.options
            ? deps.options->host_working_dir
            : deps.default_working_dir;
    return launch;
}

float imgui_font_size_from_metrics(const FontMetrics& metrics)
{
    return static_cast<float>(metrics.ascender + metrics.descender);
}

std::string legacy_pane_id_for_leaf(LeafId leaf_id)
{
    return "pane-" + std::to_string(static_cast<int>(leaf_id));
}

bool parse_generated_pane_id(std::string_view text, uint64_t* value)
{
    constexpr std::string_view prefix = "pane-";
    if (!text.starts_with(prefix))
        return false;

    uint64_t parsed = 0;
    const auto number = text.substr(prefix.size());
    const auto result = std::from_chars(number.data(), number.data() + number.size(), parsed);
    if (result.ec != std::errc() || result.ptr != number.data() + number.size())
        return false;
    *value = parsed;
    return true;
}

} // namespace

PaneManager::PaneManager(Deps deps)
    : deps_(std::move(deps))
{
}

HostKind PaneManager::platform_default_split_host_kind()
{
    return platform_default_split_host_kind_impl();
}

HostKind PaneManager::split_host_kind_for(HostKind primary_kind)
{
    if (is_terminal_shell_host(primary_kind))
        return primary_kind;
    return platform_default_split_host_kind_impl();
}

bool PaneManager::create(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
    std::optional<HostKind> host_kind_override)
{
    PERF_MEASURE();
    error_.clear();
    hosts_.clear();
    launch_options_.clear();
    pane_user_names_.clear();
    pane_ids_.clear();
    agent_identities_.clear();
    runtime_generations_.clear();
    runtime_started_at_.clear();
    next_runtime_generation_ = 1;
    next_pane_serial_ = 1;

    LeafId root_id = tree_.reset(pixel_w, pixel_h);

    HostLaunchOptions launch;
    launch.kind = host_kind_override.value_or(deps_.options->host_kind);
    const bool inherit_host_specific_options = !host_kind_override || *host_kind_override == deps_.options->host_kind;
    if (inherit_host_specific_options)
    {
        launch.command = deps_.options->host_command;
        launch.args = deps_.options->host_args;
        launch.source_path = deps_.options->host_source_path;
        launch.startup_commands = deps_.options->startup_commands;
    }
    launch.working_dir = deps_.default_working_dir.empty()
        ? deps_.options->host_working_dir
        : deps_.default_working_dir;
    launch.enable_ligatures = deps_.config->enable_ligatures;
    apply_terminal_config(launch, *deps_.config);
    if (deps_.options)
        apply_global_host_options(launch, *deps_.options);

    return create_host_for_leaf(root_id, callbacks, std::move(launch), true);
}

LeafId PaneManager::split_focused(SplitDirection dir, IHostCallbacks& callbacks)
{
    PERF_MEASURE();
    LeafId focused = tree_.focused();
    if (focused == kInvalidLeaf)
        return kInvalidLeaf;

    LeafId new_id = tree_.split_leaf(focused, dir);
    if (new_id == kInvalidLeaf)
        return kInvalidLeaf;

    HostLaunchOptions launch;
    // Split panes open a shell by default. If the primary host is already a shell,
    // preserve that explicit shell choice; otherwise use the platform shell.
    const HostKind primary_kind = deps_.options ? deps_.options->host_kind : platform_default_split_host_kind_impl();
    launch.kind = split_host_kind_for(primary_kind);
    launch.enable_ligatures = deps_.config->enable_ligatures;
    apply_terminal_config(launch, *deps_.config);
    if (deps_.options)
        apply_global_host_options(launch, *deps_.options);
    if (deps_.options)
    {
        launch.working_dir = deps_.default_working_dir.empty()
            ? deps_.options->host_working_dir
            : deps_.default_working_dir;
        if (is_terminal_shell_host(primary_kind) && launch.kind == primary_kind)
        {
            launch.command = deps_.options->host_command;
            launch.args = deps_.options->host_args;
            launch.startup_commands = deps_.options->startup_commands;
        }
    }

    if (!create_host_for_leaf(new_id, callbacks, std::move(launch), false))
    {
        // Rollback the tree split
        tree_.close_leaf(new_id);
        return kInvalidLeaf;
    }

    // Update all viewports (tree was recomputed by split_leaf)
    update_all_viewports();

    // Focus the new pane
    update_focus(new_id);

    return new_id;
}

LeafId PaneManager::split_focused(SplitDirection dir, HostKind kind, IHostCallbacks& callbacks)
{
    HostLaunchOptions launch;
    launch.kind = kind;
    return split_focused(dir, std::move(launch), callbacks);
}

LeafId PaneManager::split_focused(SplitDirection dir, HostLaunchOptions launch, IHostCallbacks& callbacks)
{
    PERF_MEASURE();
    LeafId focused = tree_.focused();
    if (focused == kInvalidLeaf)
        return kInvalidLeaf;

    LeafId new_id = tree_.split_leaf(focused, dir);
    if (new_id == kInvalidLeaf)
        return kInvalidLeaf;

    launch.enable_ligatures = deps_.config->enable_ligatures;
    apply_terminal_config(launch, *deps_.config);
    if (deps_.options)
        apply_global_host_options(launch, *deps_.options);
    if (launch.working_dir.empty())
        launch.working_dir = deps_.default_working_dir.empty() && deps_.options
            ? deps_.options->host_working_dir
            : deps_.default_working_dir;

    if (!create_host_for_leaf(new_id, callbacks, std::move(launch), false))
    {
        tree_.close_leaf(new_id);
        return kInvalidLeaf;
    }

    update_all_viewports();
    update_focus(new_id);
    return new_id;
}

bool PaneManager::close_leaf(LeafId id)
{
    PERF_MEASURE();
    if (tree_.leaf_count() <= 1)
        return false;

    auto it = hosts_.find(id);
    if (it == hosts_.end())
        return false;

    // Cancel zoom if the zoomed pane is being closed, or if closing reduces to one leaf.
    if (zoomed_ && (id == zoomed_leaf_ || tree_.leaf_count() <= 2))
    {
        zoomed_ = false;
        zoomed_leaf_ = kInvalidLeaf;
    }

    // Drop companion-preview tracking if either the preview or its owner is the
    // pane being closed, so the ids can never dangle.
    if (id == markdown_preview_leaf_ || id == markdown_preview_owner_)
    {
        markdown_preview_leaf_ = kInvalidLeaf;
        markdown_preview_owner_ = kInvalidLeaf;
    }

    // Shut down the host
    if (it->second)
        it->second->shutdown();
    hosts_.erase(it);
    launch_options_.erase(id);
    pane_user_names_.erase(id);
    pane_ids_.erase(id);
    agent_identities_.erase(id);
    runtime_generations_.erase(id);
    runtime_started_at_.erase(id);

    // Collapse the tree (this also updates focus if needed)
    LeafId old_focus = tree_.focused();
    if (!tree_.close_leaf(id))
        return false;

    update_all_viewports();

    // The tree may have shifted focus to the surviving leaf. Notify it so
    // the cursor blinker restarts and the cursor becomes visible.
    LeafId new_focus = tree_.focused();
    if (new_focus != kInvalidLeaf && new_focus != old_focus)
    {
        if (IHost* h = host_for(new_focus))
            h->on_focus_gained();
    }

    return true;
}

bool PaneManager::close_focused()
{
    return close_leaf(tree_.focused());
}

bool PaneManager::restart_focused(IHostCallbacks& callbacks)
{
    PERF_MEASURE();
    LeafId id = tree_.focused();
    if (id == kInvalidLeaf)
        return false;

    auto it = hosts_.find(id);
    if (it == hosts_.end())
        return false;

    // Retrieve saved launch options for this leaf.
    auto opts_it = launch_options_.find(id);
    if (opts_it == launch_options_.end())
    {
        error_ = "No launch options recorded for focused pane.";
        return false;
    }
    HostLaunchOptions launch = opts_it->second;

    // Shut down the current host.
    if (it->second)
        it->second->shutdown();
    hosts_.erase(it);

    // Relaunch the same host in the same pane slot.
    if (!create_host_for_leaf(id, callbacks, std::move(launch), false))
        return false;

    update_all_viewports();
    return true;
}

bool PaneManager::swap_focused_with_next()
{
    PERF_MEASURE();
    LeafId focused = tree_.focused();
    if (focused == kInvalidLeaf)
        return false;

    LeafId next = tree_.next_leaf_after(focused);
    if (next == kInvalidLeaf)
        return false;

    // swap_leaves swaps the IDs stored in the tree nodes: the node at
    // position-A now holds ID B and vice versa.  update_all_viewports()
    // iterates the tree in spatial order, so hosts_[B] will receive
    // position-A's viewport — effectively swapping the two hosts'
    // on-screen positions.  The hosts_ and launch_options_ maps stay
    // unchanged because the keys still match the (now relocated) IDs.
    if (!tree_.swap_leaves(focused, next))
        return false;

    update_all_viewports();
    return true;
}

void PaneManager::recompute_viewports(int pixel_w, int pixel_h)
{
    recompute_viewports(0, 0, pixel_w, pixel_h);
}

void PaneManager::recompute_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    tree_.recompute(origin_x, origin_y, pixel_w, pixel_h);
    if (zoomed_)
    {
        zoom_pixel_w_ = pixel_w;
        zoom_pixel_h_ = pixel_h;
    }
    update_all_viewports();
}

void PaneManager::toggle_zoom(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    if (tree_.leaf_count() <= 1)
        return; // Nothing to zoom with a single pane.

    if (zoomed_)
    {
        // Unzoom: restore normal viewports.
        zoomed_ = false;
        zoomed_leaf_ = kInvalidLeaf;
        update_all_viewports();
    }
    else
    {
        // Zoom: focused pane fills the full window.
        LeafId focused = tree_.focused();
        if (focused == kInvalidLeaf)
            return;

        zoomed_ = true;
        zoomed_leaf_ = focused;
        zoom_pixel_w_ = pixel_w;
        zoom_pixel_h_ = pixel_h;
        update_all_viewports();
    }
}

LeafId PaneManager::show_markdown_preview(
    LeafId owner, float top_ratio, std::string_view path, IHostCallbacks& callbacks)
{
    PERF_MEASURE();

    // Already open: just reload the source in place, leaving the split alone.
    if (markdown_preview_leaf_ != kInvalidLeaf)
    {
        if (IHost* preview = host_for(markdown_preview_leaf_))
        {
            preview->dispatch_action(std::string("open_file:") + std::string(path));
            return markdown_preview_leaf_;
        }
        // The preview leaf vanished (e.g. closed elsewhere) — fall through and
        // recreate it.
        markdown_preview_leaf_ = kInvalidLeaf;
        markdown_preview_owner_ = kInvalidLeaf;
    }

    if (host_for(owner) == nullptr)
        return kInvalidLeaf;

    // split_focused() splits the focused leaf; point it at the owner first.
    const LeafId prev_focus = tree_.focused();
    tree_.set_focused(owner);

    HostLaunchOptions launch;
    launch.kind = HostKind::Markdown;
    launch.source_path = std::string(path);
    const LeafId preview = split_focused(SplitDirection::Horizontal, std::move(launch), callbacks);
    if (preview == kInvalidLeaf)
    {
        update_focus(prev_focus == kInvalidLeaf ? owner : prev_focus);
        return kInvalidLeaf;
    }

    // The owner is the first (top) child, so `top_ratio` of the height stays
    // with it and the preview takes the bottom remainder.
    const DividerId divider = tree_.find_ancestor_divider(preview, FocusDirection::Up);
    if (divider != kInvalidDivider)
    {
        tree_.set_divider_ratio(divider, top_ratio);
        update_all_viewports();
    }

    markdown_preview_leaf_ = preview;
    markdown_preview_owner_ = owner;

    // Keep input focus on the owner (Kanban) rather than the new preview pane.
    update_focus(owner);
    return preview;
}

void PaneManager::hide_markdown_preview()
{
    PERF_MEASURE();
    if (markdown_preview_leaf_ == kInvalidLeaf)
        return;

    const LeafId preview = markdown_preview_leaf_;
    const LeafId owner = markdown_preview_owner_;
    markdown_preview_leaf_ = kInvalidLeaf;
    markdown_preview_owner_ = kInvalidLeaf;

    // close_leaf() collapses the split and shifts focus to the surviving
    // sibling (the owner); make focus explicit in case the tree chose otherwise.
    close_leaf(preview);
    if (owner != kInvalidLeaf && host_for(owner))
        update_focus(owner);
}

void PaneManager::shutdown()
{
    PERF_MEASURE();
    for (auto& [id, host] : hosts_)
    {
        if (host)
        {
            host->shutdown();
            host.reset();
        }
    }
    hosts_.clear();
    launch_options_.clear();
    pane_user_names_.clear();
    pane_ids_.clear();
    agent_identities_.clear();
    runtime_generations_.clear();
    runtime_started_at_.clear();
    next_runtime_generation_ = 1;
    next_pane_serial_ = 1;
    markdown_preview_leaf_ = kInvalidLeaf;
    markdown_preview_owner_ = kInvalidLeaf;
}

bool PaneManager::has_restorable_shell_session() const
{
    if (hosts_.empty() || launch_options_.empty())
        return false;

    for (const auto& [id, launch] : launch_options_)
    {
        if (!hosts_.contains(id))
            return false;
        if (!is_terminal_shell_host(launch.kind))
            return false;
    }

    return true;
}

bool PaneManager::should_preserve_dead_leaf(LeafId id) const
{
    const auto host_it = hosts_.find(id);
    const auto launch_it = launch_options_.find(id);
    if (host_it == hosts_.end() || launch_it == launch_options_.end() || !host_it->second)
        return false;
    if (host_it->second->is_running())
        return false;
    if (is_terminal_shell_host(launch_it->second.kind))
    {
        // Preserve shell panes only if they exited abnormally (non-zero exit
        // code). Clean exits (code 0) and unknown exit codes (race between
        // process exit and waitpid reap) close immediately.
        const std::optional<int> exit_code = host_it->second->exit_code();
        return exit_code.has_value() && *exit_code != 0;
    }
    // Non-shell hosts (nvim, megacity) are always preserved when they die.
    return true;
}

std::optional<PaneManager::PaneLayoutSnapshot> PaneManager::snapshot_layout() const
{
    PERF_MEASURE();
    if (hosts_.empty() || launch_options_.empty())
        return std::nullopt;

    PaneLayoutSnapshot state;
    state.tree = tree_.snapshot();
    state.zoomed = zoomed_;
    state.zoomed_leaf = zoomed_leaf_;
    bool valid = true;

    tree_.for_each_leaf([this, &state, &valid](LeafId id, const PaneDescriptor&) {
        if (!valid)
            return;

        const auto launch_it = launch_options_.find(id);
        const auto host_it = hosts_.find(id);
        if (launch_it == launch_options_.end() || host_it == hosts_.end() || !host_it->second)
        {
            valid = false;
            return;
        }

        PaneSnapshot pane;
        pane.leaf_id = id;
        pane.launch = save_launch_options(launch_it->second);
        const std::string current_cwd = host_it->second->current_working_directory();
        if (!current_cwd.empty())
            pane.launch.working_dir = current_cwd;
        pane.pane_name = pane_name(id);
        pane.pane_id = pane_id(id);
        if (const AgentIdentity* agent = agent_identity(id))
            pane.agent = *agent;
        state.panes.push_back(std::move(pane));
    });

    if (!valid)
        return std::nullopt;

    return state;
}

bool PaneManager::restore_layout(
    IHostCallbacks& callbacks, int pixel_w, int pixel_h, const PaneLayoutSnapshot& state)
{
    PERF_MEASURE();
    error_.clear();
    shutdown();

    if (!tree_.restore(state.tree, pixel_w, pixel_h))
    {
        error_ = "Failed to restore the saved split layout.";
        return false;
    }

    if (state.panes.empty())
    {
        error_ = "Saved session has no panes.";
        return false;
    }

    bool is_primary = true;
    next_pane_serial_ = 1;
    const auto leaf_exists = [this](LeafId target) {
        bool found = false;
        tree_.for_each_leaf([&found, target](LeafId id, const PaneDescriptor&) {
            if (id == target)
                found = true;
        });
        return found;
    };
    for (const PaneSnapshot& pane : state.panes)
    {
        if (!leaf_exists(pane.leaf_id))
        {
            error_ = "Saved session references an unknown pane id.";
            shutdown();
            return false;
        }

        HostLaunchOptions launch = restore_launch_options(pane.launch, deps_);
        pane_ids_[pane.leaf_id] = pane.pane_id.empty()
            ? legacy_pane_id_for_leaf(pane.leaf_id)
            : pane.pane_id;
        uint64_t parsed_id = 0;
        if (parse_generated_pane_id(pane_ids_[pane.leaf_id], &parsed_id))
            next_pane_serial_ = std::max(next_pane_serial_, parsed_id + 1);
        if (!create_host_for_leaf(pane.leaf_id, callbacks, std::move(launch), is_primary))
        {
            shutdown();
            return false;
        }
        is_primary = false;

        if (!pane.pane_name.empty())
            pane_user_names_[pane.leaf_id] = pane.pane_name;
        if (pane.agent)
            agent_identities_[pane.leaf_id] = *pane.agent;
    }

    zoomed_leaf_ = state.zoomed && leaf_exists(state.zoomed_leaf)
        ? state.zoomed_leaf
        : kInvalidLeaf;
    zoomed_ = zoomed_leaf_ != kInvalidLeaf;
    if (!zoomed_)
        zoomed_leaf_ = kInvalidLeaf;
    zoom_pixel_w_ = pixel_w;
    zoom_pixel_h_ = pixel_h;
    update_all_viewports();
    return true;
}

void PaneManager::set_pane_name(LeafId id, std::string name)
{
    if (name.empty())
        pane_user_names_.erase(id);
    else
        pane_user_names_[id] = std::move(name);
}

const std::string& PaneManager::pane_name(LeafId id) const
{
    static const std::string empty;
    auto it = pane_user_names_.find(id);
    return it == pane_user_names_.end() ? empty : it->second;
}

bool PaneManager::has_pane_name(LeafId id) const
{
    return pane_user_names_.find(id) != pane_user_names_.end();
}

const std::string& PaneManager::pane_id(LeafId id) const
{
    static const std::string empty;
    auto it = pane_ids_.find(id);
    return it == pane_ids_.end() ? empty : it->second;
}

void PaneManager::set_agent_identity(LeafId id, AgentIdentity identity)
{
    if (!hosts_.contains(id) || identity.kind.empty()
        || identity.display_name.empty() || identity.instance_id.empty())
    {
        return;
    }
    agent_identities_[id] = std::move(identity);
}

const AgentIdentity* PaneManager::agent_identity(LeafId id) const
{
    const auto it = agent_identities_.find(id);
    return it == agent_identities_.end() ? nullptr : &it->second;
}

bool PaneManager::clear_agent_identity(LeafId id)
{
    return agent_identities_.erase(id) != 0;
}

AgentRuntimeGeneration PaneManager::agent_runtime_generation(LeafId id) const
{
    const auto found = runtime_generations_.find(id);
    return found == runtime_generations_.end()
        ? AgentRuntimeGeneration{}
        : found->second;
}

std::chrono::steady_clock::time_point PaneManager::agent_runtime_started_at(
    LeafId id) const
{
    const auto found = runtime_started_at_.find(id);
    return found == runtime_started_at_.end()
        ? std::chrono::steady_clock::time_point{}
        : found->second;
}

IHost* PaneManager::focused_host() const
{
    return host_for(tree_.focused());
}

void PaneManager::set_focused(LeafId id)
{
    update_focus(id);
}

bool PaneManager::focus_direction(FocusDirection direction)
{
    PERF_MEASURE();
    LeafId current = tree_.focused();
    if (current == kInvalidLeaf)
        return false;

    LeafId neighbor = tree_.find_neighbor(current, direction);
    if (neighbor == kInvalidLeaf)
        return false;

    update_focus(neighbor);
    return true;
}

void PaneManager::update_focus(LeafId new_id)
{
    LeafId old_id = tree_.focused();
    if (old_id == new_id)
        return;

    if (IHost* old_host = host_for(old_id))
        old_host->on_focus_lost();

    tree_.set_focused(new_id);

    if (IHost* new_host = host_for(new_id))
        new_host->on_focus_gained();
}

IHost* PaneManager::host_for(LeafId id) const
{
    auto it = hosts_.find(id);
    return it != hosts_.end() ? it->second.get() : nullptr;
}

IHost* PaneManager::host_at_point(int px, int py)
{
    PERF_MEASURE();
    auto result = tree_.hit_test(px, py);
    if (const auto* leaf_hit = std::get_if<SplitTree::LeafHit>(&result))
    {
        update_focus(leaf_hit->id);
        return host_for(leaf_hit->id);
    }
    // If divider hit or miss, return focused host
    return focused_host();
}

std::optional<PaneManager::DividerHitInfo> PaneManager::divider_at_point(int px, int py) const
{
    PERF_MEASURE();
    auto result = tree_.hit_test(px, py);
    if (const auto* div_hit = std::get_if<SplitTree::DividerHit>(&result))
        return DividerHitInfo{ div_hit->id, div_hit->direction };
    return std::nullopt;
}

namespace
{
int snap_step_for_divider(const SplitTree& tree, DividerId id, int cell_w, int cell_h)
{
    // Vertical splits move horizontally → snap to cell_w; horizontal splits
    // move vertically → snap to cell_h.
    const auto dir = tree.divider_direction(id);
    if (!dir)
        return 0;
    return *dir == SplitDirection::Vertical ? cell_w : cell_h;
}
} // namespace

void PaneManager::update_divider_from_pixel(DividerId id, int px, int py, int cell_w, int cell_h)
{
    PERF_MEASURE();
    if (zoomed_)
        return;
    // SplitTree preserves its own origin_x/origin_y/total_w/total_h from the
    // last recompute() (which reserves space for the chrome strip), so call
    // through the tree directly rather than recompute_viewports() — the latter
    // would override the chrome reservation with (0, 0) and hide the tab bar.
    const int snap = snap_step_for_divider(tree_, id, cell_w, cell_h);
    tree_.update_divider_from_pixel(id, px, py, snap);
    update_all_viewports();
}

void PaneManager::nudge_divider(DividerId id, float delta, int cell_w, int cell_h)
{
    PERF_MEASURE();
    if (zoomed_)
        return;
    const int snap = snap_step_for_divider(tree_, id, cell_w, cell_h);
    tree_.nudge_divider(id, delta, snap);
    update_all_viewports();
}

DividerId PaneManager::find_focused_ancestor_divider(FocusDirection direction) const
{
    return tree_.find_ancestor_divider(tree_.focused(), direction);
}

bool PaneManager::create_host_for_leaf(LeafId id, IHostCallbacks& callbacks,
    HostLaunchOptions launch, bool is_primary)
{
    PERF_MEASURE();
    if (!pane_ids_.contains(id))
    {
        for (;;)
        {
            const std::string candidate = "pane-" + std::to_string(next_pane_serial_++);
            const bool in_use = std::any_of(pane_ids_.begin(), pane_ids_.end(),
                [&candidate](const auto& entry) {
                    return entry.second == candidate;
                });
            if (!in_use)
            {
                pane_ids_[id] = candidate;
                break;
            }
        }
    }

    const bool draxul_managed = std::any_of(launch.environment.begin(),
        launch.environment.end(), [](const auto& value) {
            return value.first == "DRAXUL_ENV" && value.second == "1";
        });
    if (draxul_managed)
    {
        auto pane_env = std::find_if(launch.environment.begin(), launch.environment.end(),
            [](const auto& value) { return value.first == "DRAXUL_PANE_ID"; });
        if (pane_env == launch.environment.end())
            launch.environment.emplace_back("DRAXUL_PANE_ID", pane_ids_[id]);
        else
            pane_env->second = pane_ids_[id];
    }

    std::unique_ptr<IHost> new_host;

    if (deps_.options && deps_.options->host_factory)
    {
        new_host = deps_.options->host_factory(launch.kind);
    }
    else
    {
        new_host = HostProviderRegistry::global().create(launch.kind);
    }

    if (!new_host)
    {
        pane_ids_.erase(id);
        if (HostProviderRegistry::global().has(launch.kind))
            error_ = std::string("The selected host could not be created: ") + to_string(launch.kind);
        else
            error_ = std::string("The selected host is not available in this build: ") + to_string(launch.kind);
        return false;
    }

    IGridRenderer& grid_renderer = *deps_.grid_renderer;
    const float display_ppi = deps_.display_ppi ? *deps_.display_ppi : 96.0f;

    PaneDescriptor desc = tree_.descriptor_for(id);
    HostViewport viewport = deps_.compute_viewport ? deps_.compute_viewport(desc) : HostViewport{};

    // Save launch options before moving them into the context.
    HostLaunchOptions saved_launch = launch;

    if (HostContext context{
            .window = deps_.window,
            .grid_renderer = &grid_renderer,
            .text_service = deps_.text_service,
            .config = deps_.config,
            .config_document = deps_.config_document,
            .launch_options = std::move(launch),
            .initial_viewport = viewport,
            .owner_lifetime = deps_.owner_lifetime,
            .display_ppi = display_ppi,
        };
        !new_host->initialize(context, callbacks))
    {
        pane_ids_.erase(id);
        error_ = new_host->init_error();
        if (error_.empty())
            error_ = "Failed to initialize the selected host.";
        return false;
    }

    if (deps_.text_service)
    {
        const float imgui_font_size = imgui_font_size_from_metrics(deps_.text_service->metrics());
        new_host->set_imgui_font(deps_.text_service->primary_font_path(), imgui_font_size);
    }

    if (deps_.imgui_host)
        new_host->attach_imgui_host(*deps_.imgui_host);

    if (is_primary)
        grid_renderer.set_default_background(new_host->default_background());

    hosts_[id] = std::move(new_host);
    runtime_generations_[id] = { next_runtime_generation_++ };
    runtime_started_at_[id] = std::chrono::steady_clock::now();
    launch_options_[id] = std::move(saved_launch);
    return true;
}

void PaneManager::equalize_splits(IHostCallbacks& /*callbacks*/)
{
    PERF_MEASURE();
    tree_.equalize_splits();
    update_all_viewports();
}

void PaneManager::update_all_viewports()
{
    PERF_MEASURE();
    if (!deps_.compute_viewport)
        return;

    if (zoomed_)
    {
        // When zoomed, only the focused pane gets a viewport update.
        // Hidden panes are left untouched — calling set_viewport({0,0})
        // would trigger a grid resize to 1x1 in the child process
        // (nvim, shell) which is both wasteful and disruptive.
        PaneDescriptor full_desc{ { 0, 0 }, { zoom_pixel_w_, zoom_pixel_h_ } };

        auto it = hosts_.find(zoomed_leaf_);
        if (it != hosts_.end() && it->second)
            it->second->set_viewport(deps_.compute_viewport(full_desc));
    }
    else
    {
        tree_.for_each_leaf([this](LeafId id, const PaneDescriptor& desc) {
            auto it = hosts_.find(id);
            if (it != hosts_.end() && it->second)
                it->second->set_viewport(deps_.compute_viewport(desc));
        });
    }
}

} // namespace draxul
