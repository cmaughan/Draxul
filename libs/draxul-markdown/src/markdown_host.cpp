#include <draxul/markdown/markdown_host.h>

#include <algorithm>
#include <draxul/app_config_types.h>
#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>
#include <draxul/markdown/markdown_parser.h>
#include <SDL3/SDL.h>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

namespace draxul::markdown
{
namespace
{

constexpr float kScrollbarWidth = 10.0f;
constexpr float kScrollbarInset = 4.0f;

TextServiceConfig text_config_from_context(const HostContext& context)
{
    TextServiceConfig config;
    if (context.config != nullptr)
    {
        config.font_path = context.config->font_path;
        config.bold_font_path = context.config->bold_font_path;
        config.italic_font_path = context.config->italic_font_path;
        config.bold_italic_font_path = context.config->bold_italic_font_path;
        config.fallback_paths = context.config->fallback_paths;
        config.enable_ligatures = context.config->enable_ligatures;
    }

    if (config.font_path.empty() && context.text_service != nullptr)
        config.font_path = context.text_service->primary_font_path();
    return config;
}

float markdown_point_size_from_context(const HostContext& context)
{
    if (context.config != nullptr)
        return context.config->markdown.font_size;
    if (context.text_service != nullptr)
        return context.text_service->point_size();
    return TextService::DEFAULT_POINT_SIZE;
}

float markdown_margin_columns_from_context(const HostContext& context)
{
    if (context.config != nullptr)
        return context.config->markdown.margin_columns;
    return 2.0f;
}

std::filesystem::path resolve_source_path(const HostLaunchOptions& launch_options)
{
    std::filesystem::path path = launch_options.source_path;
    if (path.empty())
        return {};
    if (path.is_relative() && !launch_options.working_dir.empty())
        path = std::filesystem::path(launch_options.working_dir) / path;
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

std::string read_file_to_string(const std::filesystem::path& path, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        if (error != nullptr)
            *error = "Failed to open markdown file: " + path.string();
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool point_in_rect(glm::ivec2 point, const Rect& rect)
{
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    return x >= rect.x && x <= rect.x + rect.width
        && y >= rect.y && y <= rect.y + rect.height;
}

} // namespace

MarkdownHost::MarkdownHost() = default;

MarkdownHost::~MarkdownHost()
{
    shutdown();
}

bool MarkdownHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    config_ = context.config;
    viewport_ = context.initial_viewport;
    display_ppi_ = context.display_ppi;
    base_point_size_ = markdown_point_size_from_context(context);
    margin_columns_ = markdown_margin_columns_from_context(context);
    theme_ = default_markdown_theme(base_point_size_);
    text_config_ = text_config_from_context(context);

    if (!initialize_rich_text(context))
        return false;

    if (!load_source(context.launch_options))
        return false;

    render_pass_ = create_markdown_render_pass();
    if (!render_pass_)
    {
        init_error_ = "Failed to create markdown render pass.";
        return false;
    }

    running_ = true;
    rebuild_layout();
    rebuild_draw_list();

    if (callbacks_)
    {
        callbacks_->set_window_title(source_path_.filename().string());
        callbacks_->request_frame();
    }
    return true;
}

void MarkdownHost::shutdown()
{
    render_pass_.reset();
    rich_text_.shutdown();
    running_ = false;
}

bool MarkdownHost::is_running() const
{
    return running_;
}

std::string MarkdownHost::init_error() const
{
    return init_error_;
}

void MarkdownHost::set_viewport(const HostViewport& viewport)
{
    const bool size_changed = viewport.pixel_size != viewport_.pixel_size
        || viewport.pixel_scale != viewport_.pixel_scale;
    viewport_ = viewport;
    if (size_changed)
        mark_layout_dirty();
}

void MarkdownHost::on_config_reloaded(const HostReloadConfig& config)
{
    const bool font_changed = config.markdown_font_size != base_point_size_;
    const bool margin_changed = config.markdown_margin_columns != margin_columns_;
    if (!font_changed && !margin_changed)
        return;

    base_point_size_ = config.markdown_font_size;
    margin_columns_ = config.markdown_margin_columns;
    theme_ = default_markdown_theme(base_point_size_);
    if (font_changed)
    {
        rich_text_.shutdown();
        if (!rich_text_.initialize(text_config_, base_point_size_, display_ppi_))
        {
            init_error_ = "Failed to reinitialize markdown font service.";
            running_ = false;
            return;
        }
    }
    mark_layout_dirty();
}

void MarkdownHost::pump()
{
    if (layout_dirty_)
        rebuild_layout();
    if (draw_list_dirty_)
        rebuild_draw_list();
}

void MarkdownHost::draw(IFrameContext& frame)
{
    if (!render_pass_)
        return;

    RenderViewport viewport;
    viewport.x = viewport_.pixel_pos.x;
    viewport.y = viewport_.pixel_pos.y;
    viewport.width = viewport_.pixel_size.x;
    viewport.height = viewport_.pixel_size.y;
    frame.record_render_pass(*render_pass_, viewport);
    frame.flush_submit_chunk();
}

std::optional<std::chrono::steady_clock::time_point> MarkdownHost::next_deadline() const
{
    return std::nullopt;
}

void MarkdownHost::on_key(const KeyEvent& event)
{
    const MarkdownNavigationCommand command = navigation_.on_key(event);
    if (command == MarkdownNavigationCommand::None)
        return;

    apply_navigation_command(command);
}

void MarkdownHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (event.button != SDL_BUTTON_LEFT)
        return;

    if (!event.pressed)
    {
        scrollbar_dragging_ = false;
        return;
    }

    if (!scroll_.scrollbar_visible())
        return;

    const Rect track = scrollbar_track_rect();
    if (!point_in_rect(event.pos, track))
        return;

    const Rect thumb = scrollbar_thumb_rect();
    scrollbar_dragging_ = true;
    scrollbar_drag_grab_y_ = point_in_rect(event.pos, thumb)
        ? static_cast<float>(event.pos.y) - thumb.y
        : thumb.height * 0.5f;
    navigation_.reset();
    update_scrollbar_drag(static_cast<float>(event.pos.y));
}

void MarkdownHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (!scrollbar_dragging_)
        return;

    if ((event.buttons & SDL_BUTTON_LMASK) == 0)
    {
        scrollbar_dragging_ = false;
        return;
    }

    update_scrollbar_drag(static_cast<float>(event.pos.y));
}

void MarkdownHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    const float wheel_px = std::max(24.0f, base_point_size_ * 4.0f);
    scroll_pixels(-event.delta.y * wheel_px);
}

bool MarkdownHost::dispatch_action(std::string_view action)
{
    if (action == "reload")
        return load_source(HostLaunchOptions{ .kind = HostKind::Markdown, .source_path = source_path_.string() });
    if (action == "font_increase")
        return change_font_size(base_point_size_ + 0.5f);
    if (action == "font_decrease")
        return change_font_size(base_point_size_ - 0.5f);
    if (action == "font_reset")
        return change_font_size(TextService::DEFAULT_POINT_SIZE);
    return false;
}

void MarkdownHost::request_close()
{
    running_ = false;
}

std::string MarkdownHost::status_text() const
{
    return status_;
}

Color MarkdownHost::default_background() const
{
    return theme_.document_background;
}

HostRuntimeState MarkdownHost::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = running_ && !layout_dirty_ && !draw_list_dirty_;
    return state;
}

HostDebugState MarkdownHost::debug_state() const
{
    HostDebugState state;
    state.name = "Markdown";
    state.grid_cols = 0;
    state.grid_rows = static_cast<int>(layout_.rows.size());
    return state;
}

bool MarkdownHost::load_source(const HostLaunchOptions& launch_options)
{
    const auto path = resolve_source_path(launch_options);
    if (path.empty())
    {
        init_error_ = "Markdown host requires --source <markdown-file>.";
        return false;
    }

    std::string read_error;
    std::string source = read_file_to_string(path, &read_error);
    if (!read_error.empty())
    {
        init_error_ = read_error;
        return false;
    }

    auto parsed = parse_markdown(path, std::move(source));
    if (!parsed.ok)
    {
        init_error_ = parsed.error.empty() ? "Failed to parse markdown." : parsed.error;
        return false;
    }

    source_path_ = path;
    document_ = std::move(parsed.document);
    status_ = "markdown | " + source_path_.filename().string();
    mark_layout_dirty();
    return true;
}

bool MarkdownHost::initialize_rich_text(const HostContext& /*context*/)
{
    if (!rich_text_.initialize(text_config_, base_point_size_, display_ppi_))
    {
        const std::string attempted = text_config_.font_path.empty() ? "(auto-detected)" : text_config_.font_path;
        init_error_ = "Failed to initialize markdown font service: " + attempted;
        return false;
    }
    return true;
}

bool MarkdownHost::change_font_size(float point_size)
{
    point_size = std::clamp(point_size, TextService::MIN_POINT_SIZE, TextService::MAX_POINT_SIZE);
    if (point_size == base_point_size_)
        return true;

    base_point_size_ = point_size;
    theme_ = default_markdown_theme(base_point_size_);
    rich_text_.shutdown();
    if (!rich_text_.initialize(text_config_, base_point_size_, display_ppi_))
    {
        init_error_ = "Failed to reinitialize markdown font service.";
        running_ = false;
        return true;
    }

    if (config_ != nullptr)
        config_->markdown.font_size = base_point_size_;
    mark_layout_dirty();
    return true;
}

void MarkdownHost::rebuild_layout()
{
    const float width = static_cast<float>(std::max(1, viewport_.pixel_size.x));
    const float height = static_cast<float>(std::max(1, viewport_.pixel_size.y));
    layout_ = layout_markdown_document(document_, theme_, rich_text_,
        LayoutOptions{
            .viewport_width = width,
            .viewport_height = height,
            .pixel_scale = viewport_.pixel_scale,
            .margin_columns = margin_columns_,
        });
    scroll_.set_viewport_height(height);
    scroll_.set_content_height(layout_.content_height);
    layout_dirty_ = false;
    mark_draw_list_dirty();
}

void MarkdownHost::rebuild_draw_list()
{
    if (!render_pass_)
        return;

    auto draw_document = document_with_scrollbar();
    MarkdownDrawList draw_list = build_markdown_draw_list(
        draw_document,
        theme_,
        rich_text_,
        MarkdownDrawListOptions{
            .viewport_width = std::max(1, viewport_.pixel_size.x),
            .viewport_height = std::max(1, viewport_.pixel_size.y),
            .scroll_offset = scroll_.offset(),
            .pixel_scale = viewport_.pixel_scale,
        });
    auto atlas_uploads = collect_markdown_atlas_uploads(rich_text_, draw_list.used_atlas_ids, ++atlas_upload_revision_);
    render_pass_->set_draw_list(std::move(draw_list), std::move(atlas_uploads), ++draw_revision_);
    draw_list_dirty_ = false;
}

void MarkdownHost::mark_layout_dirty()
{
    layout_dirty_ = true;
    mark_draw_list_dirty();
    if (callbacks_)
        callbacks_->request_frame();
}

void MarkdownHost::mark_draw_list_dirty()
{
    draw_list_dirty_ = true;
    if (callbacks_)
        callbacks_->request_frame();
}

void MarkdownHost::apply_navigation_command(MarkdownNavigationCommand command)
{
    const float before = scroll_.offset();
    switch (command)
    {
    case MarkdownNavigationCommand::LineDown:
        scroll_.line_down(line_scroll_pixels());
        break;
    case MarkdownNavigationCommand::LineUp:
        scroll_.line_up(line_scroll_pixels());
        break;
    case MarkdownNavigationCommand::PageDown:
        scroll_.page_down();
        break;
    case MarkdownNavigationCommand::PageUp:
        scroll_.page_up();
        break;
    case MarkdownNavigationCommand::Top:
        scroll_.home();
        break;
    case MarkdownNavigationCommand::Bottom:
        scroll_.end();
        break;
    case MarkdownNavigationCommand::None:
        break;
    }

    if (scroll_.offset() != before)
        mark_draw_list_dirty();
}

float MarkdownHost::line_scroll_pixels()
{
    if (!layout_.rows.empty())
    {
        const auto range = visible_rows(
            layout_,
            scroll_.offset(),
            static_cast<float>(std::max(1, viewport_.pixel_size.y)));
        if (range.first < layout_.rows.size())
            return std::max(1.0f, layout_.rows[range.first].height);
    }

    const auto& metrics = rich_text_.metrics_for(theme_.body.rich_text);
    return std::max(1.0f, static_cast<float>(metrics.cell_height));
}

void MarkdownHost::scroll_pixels(float pixels)
{
    const float before = scroll_.offset();
    scroll_.scroll_pixels(pixels);
    if (scroll_.offset() != before)
        mark_draw_list_dirty();
}

Rect MarkdownHost::scrollbar_track_rect() const
{
    const float viewport_width = static_cast<float>(viewport_.pixel_size.x);
    const float viewport_height = static_cast<float>(viewport_.pixel_size.y);
    return scroll_.scrollbar_track(
        viewport_width - kScrollbarWidth - kScrollbarInset,
        0.0f,
        kScrollbarWidth,
        viewport_height);
}

Rect MarkdownHost::scrollbar_thumb_rect() const
{
    const Rect track = scrollbar_track_rect();
    return scroll_.scrollbar_thumb(track.x, track.y, track.width, track.height);
}

void MarkdownHost::update_scrollbar_drag(float mouse_y)
{
    if (!scroll_.scrollbar_visible())
    {
        scrollbar_dragging_ = false;
        return;
    }

    const float before = scroll_.offset();
    const Rect track = scrollbar_track_rect();
    scroll_.drag_thumb_to(track.y, track.height, mouse_y, scrollbar_drag_grab_y_);
    if (scroll_.offset() != before)
        mark_draw_list_dirty();
}

LayoutDocument MarkdownHost::document_with_scrollbar() const
{
    LayoutDocument paint_document = layout_;
    if (!scroll_.scrollbar_visible() || viewport_.pixel_size.x <= 0 || viewport_.pixel_size.y <= 0)
        return paint_document;

    const float viewport_height = static_cast<float>(viewport_.pixel_size.y);
    const float y_origin = scroll_.offset();
    const Rect track = scrollbar_track_rect();
    const Rect thumb = scrollbar_thumb_rect();

    LayoutRow overlay;
    overlay.y = y_origin;
    overlay.height = viewport_height;
    overlay.decorations.push_back(Decoration{
        .kind = Decoration::Kind::Background,
        .x = track.x,
        .y = y_origin + track.y,
        .width = track.width,
        .height = track.height,
        .color = Color(theme_.border.r, theme_.border.g, theme_.border.b, 0.35f),
    });
    overlay.decorations.push_back(Decoration{
        .kind = Decoration::Kind::ScrollbarThumb,
        .x = thumb.x,
        .y = y_origin + thumb.y,
        .width = thumb.width,
        .height = thumb.height,
        .color = Color(theme_.accent.r, theme_.accent.g, theme_.accent.b, 0.85f),
    });
    paint_document.rows.push_back(std::move(overlay));
    std::ranges::sort(paint_document.rows, {}, [](const LayoutRow& row) { return row.y; });
    return paint_document;
}

std::unique_ptr<IHost> create_markdown_host()
{
    return std::make_unique<MarkdownHost>();
}

void register_markdown_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::Markdown, &create_markdown_host);
}

} // namespace draxul::markdown
