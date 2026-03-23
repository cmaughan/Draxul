#pragma once

#include <draxul/grid.h>
#include <draxul/types.h>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace draxul
{

// Manages text-selection state for a terminal grid.
// Receives mouse events from TerminalHostBase and maintains the selection
// anchor/extent.  Notifies the renderer via a set_overlay_cells callback and
// writes selected text to the clipboard via a write_clipboard callback.
class SelectionManager
{
public:
    struct GridPos
    {
        glm::ivec2 pos{ 0 };
    };

    struct Callbacks
    {
        // Push overlay cell updates to the renderer.
        std::function<void(std::vector<CellUpdate>)> set_overlay_cells;
        // Read a cell from the current grid.
        std::function<const Cell&(int col, int row)> get_cell;
        // Return grid dimensions.
        std::function<int()> grid_cols;
        std::function<int()> grid_rows;
        // Request a frame redraw.
        std::function<void()> request_frame;
    };

    explicit SelectionManager(Callbacks cbs);

    bool is_active() const
    {
        return sel_active_;
    }

    // Called when the left mouse button is pressed (start drag).
    void begin_drag(GridPos pos);

    // Called when the left mouse button is released (end drag).
    // Returns true if a selection became active.
    bool end_drag(GridPos pos);

    // Called on mouse-move while dragging.
    void update_drag(GridPos pos);

    // Clear the selection (overlay removed from renderer).
    void clear();

    // Extract the selected text as a plain string (newline-separated).
    std::string extract_text() const;

private:
    void update_overlay() const;

    static constexpr int kSelectionMaxCells = 8192;

    Callbacks cbs_;
    bool sel_active_ = false;
    bool sel_dragging_ = false;
    GridPos sel_start_;
    GridPos sel_end_;
};

} // namespace draxul
