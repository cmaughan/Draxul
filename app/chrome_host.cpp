#include "chrome_host.h"

#include "pane_manager.h"

#include <SDL3/SDL_keycode.h>
#include <draxul/app_config.h>
#include <draxul/text_service.h>
#include <draxul/unicode.h>

namespace draxul
{

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

bool ChromeHost::initialize(const HostContext& context, IHostCallbacks&)
{
    viewport_ = context.initial_viewport;
    running_ = vector_pass_.initialize();
    return running_;
}

void ChromeHost::shutdown()
{
    text_layer_.shutdown();
    vector_pass_.shutdown();
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

int ChromeHost::tab_bar_height() const
{
    if (!deps_.grid_renderer)
        return 0;
    const auto [cell_width, cell_height] = deps_.grid_renderer->cell_size_pixels();
    (void)cell_width;
    return cell_height + 2;
}

ChromeLayoutInput ChromeHost::build_layout_input() const
{
    ChromeLayoutInput input;
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

    const TabController* controller = active_tabs();
    const bool have_tabs = controller && !controller->empty();
    const bool have_resources = deps_.system_resource_snapshot
        && deps_.system_resource_snapshot->available();
    input.show_top_bar = deps_.grid_renderer && (have_tabs || have_resources);
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
            input.dividers.push_back({
                { static_cast<float>(rect.x), static_cast<float>(rect.y),
                    static_cast<float>(rect.w), static_cast<float>(rect.h) },
                rect.direction });
        });
        const LeafId focused = tree.focused();
        if (focused != kInvalidLeaf)
        {
            const auto descriptor = tree.descriptor_for(focused);
            if (descriptor.pixel_size.x > 0 && descriptor.pixel_size.y > 0)
            {
                input.focus_rect = ChromeRect{ static_cast<float>(descriptor.pixel_pos.x),
                    static_cast<float>(descriptor.pixel_pos.y),
                    static_cast<float>(descriptor.pixel_size.x),
                    static_cast<float>(descriptor.pixel_size.y) };
            }
        }
    }

    input.show_status = deps_.config && deps_.config->show_pane_status;
    if (input.show_status && deps_.grid_renderer && input.cell_width > 0 && input.cell_height > 0
        && controller)
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
                if (!host || descriptor.pixel_size.x <= 0
                    || descriptor.pixel_size.y <= input.cell_height)
                    return;
                std::string override_name;
                if (deps_.get_pane_name)
                    override_name = deps_.get_pane_name(leaf);
                std::string text = override_name.empty() ? host->status_text() : std::move(override_name);
                if (!host->is_running() && text.find("[exited]") == std::string::npos)
                {
                    if (!text.empty())
                        text += " ";
                    text += "[exited]";
                }
                input.panes.push_back({ descriptor.pixel_pos.x, descriptor.pixel_pos.y,
                    descriptor.pixel_size.x, descriptor.pixel_size.y, index++, std::move(text),
                    leaf == focused, leaf });
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
    if (last_layout_.bar_height == 0 && last_layout_.dividers.empty()
        && last_layout_.panes.empty())
        return;
    vector_pass_.record(frame, last_layout_, input.theme,
        viewport_.pixel_size.x, viewport_.pixel_size.y);
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
    if (commit.target == RenameTarget::Tab)
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

std::optional<std::chrono::steady_clock::time_point> ChromeHost::next_deadline() const
{
    if (!rename_editor_.active())
        return std::nullopt;
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

} // namespace draxul
