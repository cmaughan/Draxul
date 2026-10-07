#include "chrome_host.h"
#include <draxul/personal_agent_store.h>

#include "agent_controller.h"
#include "pane_manager.h"

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <draxul/app_config.h>
#include <draxul/text_service.h>
#include <draxul/unicode.h>

namespace draxul
{
namespace
{
// Coins spin from the server-measured token rate when the agent is attributed
// to a native session, using TokenFu's scale (full speed at 50k tokens/s).
// Without a measurement a working agent spins at ~1 rotation/second; a
// measured agent that is working between token records turns slowly so it
// still reads as busy. A starting agent turns slowly.
constexpr double kCoinFullLoadTokensPerSecond = 50'000.0;
constexpr float kWorkingCoinLoad = 0.30f;
constexpr float kWorkingBetweenRecordsCoinLoad = 0.01f;
constexpr float kStartingCoinLoad = 0.03f;
constexpr float kDimmedCoinBrightness = 0.45f;
constexpr auto kCoinFrameInterval = std::chrono::milliseconds(33);
static_assert(static_cast<int>(ChromeCoinStyle::Codex) == static_cast<int>(ActivityCoinStyle::Codex)
    && static_cast<int>(ChromeCoinStyle::Claude) == static_cast<int>(ActivityCoinStyle::Claude)
    && static_cast<int>(ChromeCoinStyle::Grok) == static_cast<int>(ActivityCoinStyle::Grok)
    && static_cast<int>(ChromeCoinStyle::Neutral) == static_cast<int>(ActivityCoinStyle::Neutral));

ChromeCoinStyle coin_style_for_kind(std::string_view kind)
{
    if (kind == "codex")
        return ChromeCoinStyle::Codex;
    if (kind == "claude")
        return ChromeCoinStyle::Claude;
    if (kind == "grok")
        return ChromeCoinStyle::Grok;
    return ChromeCoinStyle::Neutral;
}

ChromeAgentCoinInput agent_coin_input(const AgentProjection& agent)
{
    ChromeAgentCoinInput coin;
    coin.style = coin_style_for_kind(agent.identity.kind);
    coin.dimmed = !agent.running || agent.lifecycle == AgentLifecycle::Exited
        || agent.lifecycle == AgentLifecycle::Failed;
    if (coin.dimmed)
        return coin;
    if (agent.lifecycle == AgentLifecycle::Starting)
    {
        coin.load = kStartingCoinLoad;
        return coin;
    }
    const bool working = agent.status == AgentStatus::Working;
    if (!agent.activity)
    {
        coin.load = working ? kWorkingCoinLoad : 0.0f;
        return coin;
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const double rate = agent_activity_tokens_per_second(*agent.activity, now_ms);
    coin.load = static_cast<float>(std::clamp(rate / kCoinFullLoadTokensPerSecond, 0.0, 1.0));
    if (coin.load <= 0.0f && working)
        coin.load = kWorkingBetweenRecordsCoinLoad;
    return coin;
}
} // namespace

ChromePaneStatus resolve_chrome_pane_status(std::string_view display_name,
    std::string_view host_status, bool show_status, bool running)
{
    ChromePaneStatus result;
    constexpr std::string_view kBuildFailure = "BUILD FAILED";
    const size_t failure_offset = host_status.find(kBuildFailure);
    result.attention = failure_offset != std::string_view::npos;
    if (result.attention)
    {
        result.text = host_status.substr(failure_offset);
        return result;
    }
    if (!show_status)
        return result;

    result.text = display_name;
    if (!running && result.text.find("[exited]") == std::string::npos)
    {
        if (!result.text.empty())
            result.text += " ";
        result.text += "[exited]";
    }
    return result;
}

ChromeHost::ChromeHost(Deps deps)
    : deps_(std::move(deps))
    , text_layer_(deps_.grid_renderer, deps_.text_service)
{
}

const TabController* ChromeHost::active_tabs() const noexcept
{
    if (!deps_.space_controller)
        return nullptr;
    const Space* space = deps_.space_controller->find_active_space();
    return space ? &space->tab_controller : nullptr;
}

const ChromeTheme& ChromeHost::theme() const
{
    static const ChromeTheme defaults;
    return deps_.config ? deps_.config->chrome : defaults;
}

ChromeLayoutOutput ChromeHost::layout_snapshot() const
{
    return compute_chrome_layout(build_layout_input());
}

bool ChromeHost::initialize(const HostContext& context, IHostCallbacks&)
{
    viewport_ = context.initial_viewport;
    running_ = vector_pass_.initialize();
    coin_pass_ = create_activity_coin_pass();
    return running_;
}

void ChromeHost::shutdown()
{
    text_layer_.shutdown();
    vector_pass_.shutdown();
    coin_pass_.reset();
    coin_motion_.clear();
    coins_in_motion_ = false;
    last_layout_ = {};
    running_ = false;
}

bool ChromeHost::is_running() const
{
    return running_;
}

void ChromeHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
}

const SplitTree& ChromeHost::active_tree() const
{
    if (const TabController* controller = active_tabs())
    {
        for (const auto& tab : controller->tabs())
        {
            if (tab->id == controller->active_tab_id())
                return tab->pane_manager.tree();
        }
    }
    static const SplitTree empty;
    return empty;
}

ChromeLayoutInput ChromeHost::build_layout_input() const
{
    ChromeLayoutInput input;
    input.shell_layout = shell_layout_;
    input.viewport_width = viewport_.pixel_size.x;
    input.viewport_height = viewport_.pixel_size.y;
    input.theme = theme();
    input.rename = rename_editor_.snapshot();
    input.focus_border = deps_.config ? deps_.config->focus_border_width : 3.0f;
    if (deps_.grid_renderer)
    {
        const auto [cell_width, cell_height] = deps_.grid_renderer->cell_size_pixels();
        input.cell_width = cell_width;
        input.cell_height = cell_height;
        // Chrome geometry historically uses the renderer's fixed four-pixel
        // cell inset. Keep hit regions and overlay viewports on that contract;
        // IGridRenderer::padding() is not part of Chrome's layout API and test
        // renderers may use a different internal padding value.
        input.grid_padding = kChromeGridPadding;
    }

    if (shell_layout_.sidebar_visible && deps_.space_controller)
    {
        input.spaces.reserve(deps_.space_controller->spaces().size());
        for (const auto& space : deps_.space_controller->spaces())
        {
            input.spaces.push_back(
                { space->id, space->name, space->id == deps_.space_controller->active_space_id() });
        }
        // Frame-scoped: draw() and the sidebar/tab hit tests all land here in
        // the same frame, so they share one evaluation.
        static const std::vector<AgentProjection> kNoAgents;
        const std::vector<AgentProjection>& agents = deps_.agent_controller
            ? deps_.agent_controller->frame_agents(*deps_.space_controller)
            : kNoAgents;
        for (const AgentProjection& agent : agents)
        {
            if (!agent.personal_agent_id.empty()) continue;
            std::string suffix;
            if (agent.lifecycle == AgentLifecycle::Failed)
                suffix = agent.exit_code ? "[failed " + std::to_string(*agent.exit_code) + "]"
                                         : "[failed]";
            else if (agent.lifecycle == AgentLifecycle::Exited)
                suffix = agent.exit_code ? "[exited " + std::to_string(*agent.exit_code) + "]"
                                         : "[exited]";
            else if (agent.lifecycle == AgentLifecycle::Starting)
                suffix = "[starting]";
            else if (agent.status == AgentStatus::Blocked)
                suffix = "[input]";
            else if (agent.status == AgentStatus::Done)
                suffix = "[done]";
            // Working and idle are conveyed by the activity coin's spin.
            input.agents.push_back({
                .instance_id = agent.identity.instance_id,
                .display_name = agent.identity.display_name,
                .status_suffix = std::move(suffix),
                .running = agent.running,
                .focused = agent.focused,
                .attention = agent.attention,
                .coin = agent_coin_input(agent),
            });
        }
    }

    if (shell_layout_.sidebar_visible && deps_.personal_agents)
    {
        if (const auto snapshot = deps_.personal_agents(); snapshot && !snapshot->root.empty())
        {
            for (const auto& definition : snapshot->agents)
            {
                const auto& active=deps_.agent_controller->frame_agents(*deps_.space_controller);
                const auto live=std::ranges::find(active,definition.id,&AgentProjection::personal_agent_id);
                input.personal_agents.push_back({
                    .instance_id = definition.id,
                    .display_name = definition.name,
                    .status_suffix = definition.error.empty() && snapshot->error.empty()
                        ? (live!=active.end() ? (live->running ? "" : "[exited]") : "[open]") : "[!]",
                    .running = live!=active.end() && live->running,
                    .focused = live!=active.end() && live->focused,
                    .attention = !definition.error.empty() || !snapshot->error.empty() || (live!=active.end() && live->attention),
                    .coin = live!=active.end() ? std::optional(agent_coin_input(*live)) : std::nullopt,
                });
            }
            input.personal_agents.push_back({ .display_name = "+", .add_button = true });
        }
    }
    const TabController* controller = active_tabs();
    const bool have_tabs = controller && !controller->empty();
    const bool have_resources = deps_.system_resource_snapshot
        && deps_.system_resource_snapshot->available();
    input.show_top_bar = shell_layout_.chrome_visible && shell_layout_.tab_bar.h > 0
        && deps_.grid_renderer && (have_tabs || have_resources);
    if (controller)
    {
        const int active_id = controller->active_tab_id();
        input.tabs.reserve(controller->tabs().size());
        for (const auto& tab : controller->tabs())
            input.tabs.push_back({ tab->id, tab->name, tab->id == active_id });
    }
    if (have_resources)
        input.resources = *deps_.system_resource_snapshot;
    if (deps_.weather_emoji && deps_.weather_temperature)
    {
        input.weather_emoji = deps_.weather_emoji();
        input.weather_temperature = deps_.weather_temperature();
    }
    if (deps_.chord_indicator)
        input.chord = deps_.chord_indicator();

    const SplitTree& tree = active_tree();
    if (tree.leaf_count() >= 2)
    {
        tree.for_each_divider([&](const SplitTree::DividerRect& rect) {
            input.dividers.push_back({ { static_cast<float>(rect.x), static_cast<float>(rect.y),
                                           static_cast<float>(rect.w), static_cast<float>(rect.h) },
                rect.direction });
        });
    }

    input.show_status = deps_.config && deps_.config->show_pane_status;
    if (controller)
    {
        const PaneManager* manager = nullptr;
        for (const auto& tab : controller->tabs())
        {
            if (tab->id == controller->active_tab_id())
            {
                manager = &tab->pane_manager;
                break;
            }
        }
        if (manager)
        {
            const LeafId focused = manager->tree().focused();
            int index = 1;
            manager->tree().for_each_leaf([&](LeafId leaf, const PaneDescriptor& descriptor) {
                IHost* host = manager->host_for(leaf);
                if (!host || descriptor.pixel_size.x <= 0 || descriptor.pixel_size.y <= 0)
                    return;
                std::string display_name;
                if (deps_.get_pane_display_name)
                    display_name = deps_.get_pane_display_name(leaf);
                auto status = resolve_chrome_pane_status(display_name,
                    host->status_text(), input.show_status, host->is_running());
                const HostDebugState debug = host->debug_state();
                input.panes.push_back({ descriptor.pixel_pos.x, descriptor.pixel_pos.y,
                    descriptor.pixel_size.x, descriptor.pixel_size.y, index++,
                    std::move(status.text),
                    leaf == focused, leaf, host->default_background(), debug.grid_rows,
                    status.attention });
            });
        }
    }
    return input;
}

int ChromeHost::hit_test_tab(int px, int py) const
{
    const TabController* controller = active_tabs();
    if (!controller || controller->empty() || !deps_.grid_renderer)
        return 0;
    return hit_test_chrome(compute_chrome_layout(build_layout_input()),
        ChromeHitKind::Tab, px, py);
}

SpaceId ChromeHost::hit_test_space(int px, int py) const
{
    if (!deps_.space_controller || !shell_layout_.sidebar_visible
        || !deps_.grid_renderer)
        return kInvalidSpaceId;
    return static_cast<SpaceId>(hit_test_chrome(
        compute_chrome_layout(build_layout_input()), ChromeHitKind::Space, px, py));
}

int ChromeHost::hit_test_agent(int px, int py) const
{
    if (!deps_.space_controller || !deps_.grid_renderer)
        return 0;
    return hit_test_chrome(
        compute_chrome_layout(build_layout_input()), ChromeHitKind::Agent, px, py);
}

int ChromeHost::hit_test_personal_agent(int px, int py) const
{
    return hit_test_chrome(compute_chrome_layout(build_layout_input()), ChromeHitKind::PersonalAgent, px, py);
}

LeafId ChromeHost::hit_test_pane_status_pill(int px, int py) const
{
    return static_cast<LeafId>(hit_test_chrome(last_layout_, ChromeHitKind::PaneStatus, px, py));
}

void ChromeHost::draw(IFrameContext& frame)
{
    if (!vector_pass_.available())
        return;
    ChromeLayoutInput input = build_layout_input();
    last_layout_ = compute_chrome_layout(input);
    if (last_layout_.bar_height == 0 && last_layout_.sidebar_width == 0
        && last_layout_.dividers.empty() && last_layout_.panes.empty()
        && last_layout_.pane_frames.empty())
        return;
    vector_pass_.record(frame, last_layout_, input.theme,
        viewport_.pixel_size.x, viewport_.pixel_size.y);
    if (auto coins = advance_agent_coins(last_layout_); coin_pass_ && !coins.empty())
    {
        coin_pass_->set_coins(std::move(coins));
        frame.record_render_pass(*coin_pass_,
            RenderViewport{ 0, 0, viewport_.pixel_size.x, viewport_.pixel_size.y });
    }
    text_layer_.draw(frame, last_layout_, input.theme);
    frame.flush_submit_chunk();
}

int ChromeHost::tab_id_for_index(int tab_index) const
{
    const TabController* controller = active_tabs();
    if (!controller || tab_index <= 0
        || static_cast<size_t>(tab_index) > controller->tabs().size())
        return -1;
    return controller->tabs()[static_cast<size_t>(tab_index - 1)]->id;
}

void ChromeHost::begin_tab_rename(int tab_index)
{
    const int tab_id = tab_id_for_index(tab_index);
    if (tab_id >= 0)
        begin_tab_rename_by_id(tab_id);
}

void ChromeHost::begin_agent_rename(int index)
{
    if (!deps_.agent_controller || !deps_.space_controller || index < 1) return;
    const auto agents = deps_.agent_controller->frame_agents(*deps_.space_controller);
    int visible = 0;
    for (const auto& agent : agents)
    {
        if (!agent.personal_agent_id.empty() || ++visible != index) continue;
        if (auto commit = rename_editor_.commit()) apply_rename_commit(std::move(*commit));
        rename_editor_.begin_agent(agent.identity.instance_id, agent.identity.display_name);
        if (deps_.request_frame) deps_.request_frame();
        return;
    }
}

void ChromeHost::begin_personal_agent_rename(int index)
{
    const auto snapshot=deps_.personal_agents ? deps_.personal_agents() : nullptr;
    if (!snapshot || index<1 || static_cast<size_t>(index)>snapshot->agents.size()) return;
    const auto& agent=snapshot->agents[index-1];
    if (auto commit=rename_editor_.commit()) apply_rename_commit(std::move(*commit));
    rename_editor_.begin_personal_agent(agent.id,agent.name);
    if (deps_.request_frame) deps_.request_frame();
}

void ChromeHost::begin_space_rename(SpaceId space_id)
{
    if (!deps_.space_controller || space_id == kInvalidSpaceId)
        return;
    if (rename_editor_.active()
        && !(rename_editor_.editing_space()
            && rename_editor_.space_id() == space_id))
    {
        if (auto commit = rename_editor_.commit())
            apply_rename_commit(std::move(*commit));
    }
    if (const Space* space
        = deps_.space_controller->find_space(space_id))
    {
        rename_editor_.begin_space(space_id, space->name);
        if (deps_.request_frame)
            deps_.request_frame();
    }
}

void ChromeHost::begin_tab_rename_by_id(int tab_id)
{
    const TabController* controller = active_tabs();
    if (!controller)
        return;
    if (rename_editor_.active()
        && !(rename_editor_.editing_tab() && rename_editor_.tab_id() == tab_id))
    {
        if (auto commit = rename_editor_.commit())
            apply_rename_commit(std::move(*commit));
    }
    for (const auto& tab : controller->tabs())
    {
        if (tab->id == tab_id)
        {
            rename_editor_.begin_tab(tab_id, tab->name);
            if (deps_.request_frame)
                deps_.request_frame();
            return;
        }
    }
}

void ChromeHost::begin_pane_rename(LeafId leaf)
{
    if (leaf == kInvalidLeaf)
        return;
    if (rename_editor_.active()
        && !(rename_editor_.editing_pane() && rename_editor_.leaf_id() == leaf))
    {
        if (auto commit = rename_editor_.commit())
            apply_rename_commit(std::move(*commit));
    }
    std::string initial;
    if (deps_.get_pane_name)
        initial = deps_.get_pane_name(leaf);
    rename_editor_.begin_pane(leaf, std::move(initial));
    if (deps_.request_frame)
        deps_.request_frame();
}

bool ChromeHost::is_editing_space() const
{
    return rename_editor_.editing_space();
}

SpaceId ChromeHost::editing_space_id() const
{
    return static_cast<SpaceId>(rename_editor_.space_id());
}

bool ChromeHost::is_editing_tab() const
{
    return rename_editor_.editing_tab();
}

int ChromeHost::editing_tab_id() const
{
    return rename_editor_.tab_id();
}

bool ChromeHost::is_editing_pane() const
{
    return rename_editor_.editing_pane();
}

LeafId ChromeHost::editing_leaf_id() const
{
    return rename_editor_.leaf_id();
}

bool ChromeHost::is_editing() const
{
    return rename_editor_.active();
}

void ChromeHost::apply_rename_commit(RenameCommit commit)
{
    if (commit.target == RenameTarget::Agent)
    {
        if (deps_.set_agent_name)
            deps_.set_agent_name(std::move(commit.agent_instance_id), std::move(commit.text));
    }
    else if (commit.target == RenameTarget::PersonalAgent)
    {
        if (!commit.text.empty() && deps_.set_personal_agent_name)
            deps_.set_personal_agent_name(std::move(commit.personal_id),std::move(commit.text));
    }
    else if (commit.target == RenameTarget::Space)
    {
        if (!commit.text.empty() && deps_.set_space_name)
        {
            deps_.set_space_name(
                static_cast<SpaceId>(commit.space_id),
                std::move(commit.text));
        }
    }
    else if (commit.target == RenameTarget::Tab)
    {
        if (!commit.text.empty() && deps_.set_tab_name)
            deps_.set_tab_name(commit.tab_id, std::move(commit.text));
    }
    else if (commit.target == RenameTarget::Pane && deps_.set_pane_name)
    {
        deps_.set_pane_name(commit.leaf_id, std::move(commit.text));
    }
    if (deps_.request_frame)
        deps_.request_frame();
}

void ChromeHost::commit_tab_rename()
{
    if (auto commit = rename_editor_.commit())
        apply_rename_commit(std::move(*commit));
}

void ChromeHost::cancel_tab_rename()
{
    if (!rename_editor_.active())
        return;
    rename_editor_.cancel();
    if (deps_.request_frame)
        deps_.request_frame();
}

bool ChromeHost::on_rename_text_input(const std::string& utf8)
{
    if (!rename_editor_.active())
        return false;
    if (utf8.empty())
        return true;
    rename_editor_.insert(utf8);
    if (deps_.text_service)
    {
        for (const auto& cluster : display_clusters(utf8))
            deps_.text_service->resolve_cluster(cluster.text);
    }
    if (deps_.request_frame)
        deps_.request_frame();
    return true;
}

bool ChromeHost::on_rename_key(int sdl_keycode)
{
    if (!rename_editor_.active())
        return false;
    RenameKey key = RenameKey::Other;
    switch (sdl_keycode)
    {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        key = RenameKey::Enter;
        break;
    case SDLK_ESCAPE:
        key = RenameKey::Escape;
        break;
    case SDLK_BACKSPACE:
        key = RenameKey::Backspace;
        break;
    case SDLK_DELETE:
        key = RenameKey::Delete;
        break;
    case SDLK_LEFT:
        key = RenameKey::Left;
        break;
    case SDLK_RIGHT:
        key = RenameKey::Right;
        break;
    case SDLK_HOME:
        key = RenameKey::Home;
        break;
    case SDLK_END:
        key = RenameKey::End;
        break;
    default:
        break;
    }
    if (key == RenameKey::Enter)
    {
        if (auto commit = rename_editor_.commit())
            apply_rename_commit(std::move(*commit));
    }
    else if (key == RenameKey::Escape)
    {
        cancel_tab_rename();
    }
    else
    {
        rename_editor_.handle_key(key);
        if (key != RenameKey::Other && deps_.request_frame)
            deps_.request_frame();
    }
    return true;
}

std::vector<ActivityCoinInstance> ChromeHost::advance_agent_coins(const ChromeLayoutOutput& layout)
{
    const auto now = std::chrono::steady_clock::now();
    // Clamp so a long idle gap does not jump a resuming coin.
    const float elapsed = last_coin_tick_ == std::chrono::steady_clock::time_point{}
        ? 0.0f
        : std::clamp(std::chrono::duration<float>(now - last_coin_tick_).count(), 0.0f, 0.1f);
    last_coin_tick_ = now;

    std::vector<ActivityCoinInstance> coins;
    std::unordered_map<std::string, ActivityCoinMotion> visible;
    bool in_motion = false;
    for (const ChromeAgentLayout& agent : layout.agents)
    {
        if (!agent.coin)
            continue;
        const ChromeAgentCoinLayout& coin = *agent.coin;
        ActivityCoinMotion motion;
        if (const auto it = coin_motion_.find(agent.instance_id); it != coin_motion_.end())
            motion = it->second;
        advance_activity_coin(motion, elapsed, coin.input.load);
        in_motion = in_motion || coin.input.load > 0.0f || activity_coin_in_motion(motion);
        coins.push_back({
            .center_x = coin.center_x,
            .center_y = coin.center_y,
            .radius = coin.radius,
            .angle = motion.angle,
            .brightness = coin.input.dimmed ? kDimmedCoinBrightness : 1.0f,
            .style = static_cast<ActivityCoinStyle>(static_cast<int>(coin.input.style)),
        });
        visible.insert_or_assign(agent.instance_id, motion);
    }
    coin_motion_ = std::move(visible);
    coins_in_motion_ = in_motion;
    return coins;
}

void ChromeHost::pump()
{
    // pump() runs every loop iteration; pace coin frames to the interval
    // rather than the display refresh.
    if (coins_in_motion_ && deps_.request_frame
        && std::chrono::steady_clock::now() >= last_coin_tick_ + kCoinFrameInterval)
        deps_.request_frame();
}

std::optional<std::chrono::steady_clock::time_point> ChromeHost::next_deadline() const
{
    const auto now = std::chrono::steady_clock::now();
    if (coins_in_motion_)
        return std::max(now, last_coin_tick_ + kCoinFrameInterval);
    if (!rename_editor_.active())
        return std::nullopt;
    return now + std::chrono::milliseconds(250);
}

} // namespace draxul
