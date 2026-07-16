#pragma once
#include <SDL3/SDL.h>
#include <draxul/window.h>

struct SDL_Window;

namespace draxul
{

class SdlWindow : public IWindow
{
public:
    ~SdlWindow() override;
    void set_clamp_to_display(bool clamp) override
    {
        clamp_to_display_ = clamp;
    }
    void set_hidden(bool hidden) override
    {
        hidden_ = hidden;
    }
    bool initialize(const std::string& title, int width, int height) override;
    void normalize_render_target_window_size(int target_pixel_width, int target_pixel_height) override;
    void set_size_logical(int width, int height);
    void shutdown() override;
    bool poll_events() override;
    bool wait_events(int timeout_ms) override;
    void show() override;
    void hide() override;
    bool is_visible() const override;
    void activate() override;
    void wake() override;
    void* native_handle() override // NOSONAR cpp:S5008
    {
        return window_;
    }
    std::pair<int, int> size_logical() const override;
    std::pair<int, int> size_pixels() const override;
    float display_ppi() const override;
    void set_title(const std::string& title) override;
    void set_title_bar_color(Color color) override;
    std::string clipboard_text() const override;
    bool set_clipboard_text(const std::string& text) override;
    bool open_url(std::string_view url) override;
    void set_text_input_area(int x, int y, int w, int h) override;
    void show_open_file_dialog() override;
    void set_mouse_cursor(MouseCursor cursor) override;
    void clear_lifecycle_callbacks() override;

private:
    bool handle_event(const SDL_Event& event);
    void flush_text_input_area();
    SDL_Cursor* ensure_cursor(MouseCursor cursor);
    SDL_Window* window_ = nullptr;
    unsigned int wake_event_type_ = 0;
    unsigned int file_dialog_event_type_ = 0;
    bool clamp_to_display_ = true;
    bool hidden_ = false;
    SDL_Cursor* cursor_default_ = nullptr;
    SDL_Cursor* cursor_ew_ = nullptr;
    SDL_Cursor* cursor_ns_ = nullptr;
    SDL_Cursor* cursor_pointer_ = nullptr;
    MouseCursor active_cursor_ = MouseCursor::Default;
    bool visible_ = true;
    bool text_input_area_dirty_ = false;
    int text_input_x_ = 0;
    int text_input_y_ = 0;
    int text_input_w_ = 0;
    int text_input_h_ = 0;
#ifdef _WIN32
    void* system_tray_icon_ = nullptr;
#elif defined(__APPLE__)
    void* dock_reopen_handler_ = nullptr;
#endif
};

} // namespace draxul
