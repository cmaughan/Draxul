#pragma once

#include <draxul/host.h>
#include <draxul/markdown/markdown_document.h>
#include <draxul/markdown/markdown_draw_list.h>
#include <draxul/markdown/markdown_layout.h>
#include <draxul/markdown/markdown_navigation.h>
#include <draxul/markdown/markdown_render_pass.h>
#include <draxul/markdown/markdown_scroll.h>
#include <draxul/markdown/markdown_theme.h>
#include <draxul/rich_text_service.h>
#include <draxul/text_service.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace draxul
{

class HostProviderRegistry;

namespace markdown
{

class MarkdownHost final : public draxul::IHost
{
public:
    MarkdownHost();
    ~MarkdownHost() override;

    bool initialize(const draxul::HostContext& context, draxul::IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;

    void set_viewport(const draxul::HostViewport& viewport) override;
    void on_config_reloaded(const draxul::HostReloadConfig& config) override;
    void pump() override;
    void draw(draxul::IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_key(const draxul::KeyEvent& event) override;
    void on_mouse_button(const draxul::MouseButtonEvent& event) override;
    void on_mouse_move(const draxul::MouseMoveEvent& event) override;
    void on_mouse_wheel(const draxul::MouseWheelEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    draxul::Color default_background() const override;
    draxul::HostRuntimeState runtime_state() const override;
    draxul::HostDebugState debug_state() const override;

private:
    bool load_source(const draxul::HostLaunchOptions& launch_options);
    bool initialize_rich_text(const draxul::HostContext& context);
    bool change_font_size(float point_size);
    void rebuild_layout();
    void rebuild_draw_list();
    void mark_layout_dirty();
    void mark_draw_list_dirty();
    void apply_navigation_command(MarkdownNavigationCommand command);
    float line_scroll_pixels();
    void scroll_pixels(float pixels);
    Rect scrollbar_track_rect() const;
    Rect scrollbar_thumb_rect() const;
    void update_scrollbar_drag(float mouse_y);
    LayoutDocument document_with_scrollbar() const;

    draxul::IHostCallbacks* callbacks_ = nullptr;
    draxul::AppConfig* config_ = nullptr;
    draxul::HostViewport viewport_;
    draxul::TextServiceConfig text_config_;
    float display_ppi_ = 96.0f;
    float base_point_size_ = draxul::TextService::DEFAULT_POINT_SIZE;
    float margin_columns_ = 2.0f;

    std::filesystem::path source_path_;
    Document document_;
    MarkdownTheme theme_;
    draxul::RichTextService rich_text_;
    LayoutDocument layout_;
    MarkdownScrollState scroll_;
    MarkdownNavigationState navigation_;
    std::unique_ptr<MarkdownRenderPass> render_pass_;

    std::string init_error_;
    std::string status_;
    bool running_ = false;
    bool layout_dirty_ = true;
    bool draw_list_dirty_ = true;
    bool scrollbar_dragging_ = false;
    float scrollbar_drag_grab_y_ = 0.0f;
    uint64_t draw_revision_ = 0;
    uint64_t atlas_upload_revision_ = 0;
};

std::unique_ptr<draxul::IHost> create_markdown_host();
void register_markdown_host_provider(draxul::HostProviderRegistry& registry);

} // namespace markdown
} // namespace draxul
