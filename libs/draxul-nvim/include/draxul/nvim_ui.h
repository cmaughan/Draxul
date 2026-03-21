#pragma once
#include <draxul/events.h>
#include <draxul/grid_sink.h>
#include <draxul/highlight.h>
#include <draxul/nvim_rpc.h>
#include <draxul/types.h>
#include <functional>
#include <string>
#include <vector>

namespace draxul
{

// --- UiEventHandler ---

struct ModeInfo
{
    std::string name;
    CursorShape cursor_shape = CursorShape::Block;
    int cell_percentage = 0;
    int attr_id = 0;
    int blinkwait = 0;
    int blinkon = 0;
    int blinkoff = 0;
};

class UiEventHandler
{
public:
    void set_grid(IGridSink* grid)
    {
        grid_ = grid;
    }
    void set_highlights(HighlightTable* hl)
    {
        highlights_ = hl;
    }
    void set_options(const UiOptions* options)
    {
        options_ = options;
    }

    void process_redraw(const std::vector<MpackValue>& params);

    std::function<void()> on_flush;
    std::function<void(int, int)> on_grid_resize;
    std::function<void(int, int)> on_cursor_goto;
    std::function<void(int)> on_mode_change;
    std::function<void(const std::string&, const MpackValue&)> on_option_set;
    std::function<void(bool)> on_busy;
    std::function<void(const std::string&)> on_title;

    const std::vector<ModeInfo>& modes() const
    {
        return modes_;
    }
    int current_mode() const
    {
        return current_mode_;
    }
    int cursor_col() const
    {
        return cursor_col_;
    }
    int cursor_row() const
    {
        return cursor_row_;
    }

private:
    void handle_grid_line(const MpackValue& args);
    void handle_grid_cursor_goto(const MpackValue& args);
    void handle_grid_scroll(const MpackValue& args);
    void handle_grid_clear(const MpackValue& args);
    void handle_grid_resize(const MpackValue& args);
    void handle_hl_attr_define(const MpackValue& args);
    void handle_default_colors_set(const MpackValue& args);
    void handle_mode_info_set(const MpackValue& args);
    void handle_mode_change(const MpackValue& args);
    void handle_option_set(const MpackValue& args);
    void handle_set_title(const MpackValue& args);

    IGridSink* grid_ = nullptr;
    HighlightTable* highlights_ = nullptr;
    const UiOptions* options_ = nullptr;

    std::vector<ModeInfo> modes_;
    int current_mode_ = 0;
    int cursor_col_ = 0, cursor_row_ = 0;
};

// --- NvimInput ---

class NvimInput
{
public:
    void initialize(IRpcChannel* rpc, int cell_w, int cell_h);
    void set_cell_size(int w, int h)
    {
        cell_w_ = w;
        cell_h_ = h;
    }
    // Set the pixel origin of the grid (viewport offset + padding) so that
    // mouse coordinates are correctly mapped to grid row/col.
    void set_viewport_origin(int x, int y)
    {
        viewport_x_ = x;
        viewport_y_ = y;
    }
    // Set the grid dimensions used to clamp mouse coordinates to valid cells.
    void set_grid_size(int cols, int rows)
    {
        grid_cols_ = cols;
        grid_rows_ = rows;
    }

    void on_key(const KeyEvent& event);
    void on_text_input(const TextInputEvent& event);
    void on_text_editing(const TextEditingEvent& event) const;
    void on_mouse_button(const MouseButtonEvent& event);
    void on_mouse_move(const MouseMoveEvent& event);
    void on_mouse_wheel(const MouseWheelEvent& event);
    void paste_text(const std::string& text);

private:
    void send_input(const std::string& keys);
    std::string translate_key(int keycode, ModifierFlags mod) const;
    std::string mouse_modifiers(ModifierFlags mod) const;
    // Convert raw pixel coordinates to grid row/col, clamped to grid bounds.
    int pixel_to_col(int x) const;
    int pixel_to_row(int y) const;

    IRpcChannel* rpc_ = nullptr;
    int cell_w_ = 10, cell_h_ = 20;
    int viewport_x_ = 0, viewport_y_ = 0;
    int grid_cols_ = 0, grid_rows_ = 0;
    bool suppress_next_text_ = false;
    bool mouse_pressed_ = false;
    std::string mouse_button_;
};

} // namespace draxul
