#pragma once

#include <draxul/grid_host_base.h>
#include <draxul/mouse_reporter.h>
#include <draxul/selection_manager.h>

#include <glm/glm.hpp>

#include <optional>
#include <string_view>

namespace draxul
{

// Shared user-facing terminal surface layer for local and remote terminal
// hosts: mouse selection (drag/word/line), click-to-copy, copy-on-select,
// the vim/tmux-style keyboard copy mode, mouse-report hand-off, hyperlink
// activation, and pixel→cell mapping.
//
// LocalTerminalHost (via TerminalHostBase) and RemoteTerminalHost both derive
// from this class so the surface behaviour exists exactly once; the
// host-specific seams (where mouse reports go, how the view scrolls, where
// the terminal cursor currently is) are the small `surface_*` virtuals.
class TerminalSurfaceHostBase : public GridHostBase
{
public:
    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void on_config_reloaded(const HostReloadConfig& config) override;
    void on_focus_lost() override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    std::optional<MouseCursor> mouse_cursor_at(int px, int py) const override;
    void set_scroll_offset(float px) override;

protected:
    TerminalSurfaceHostBase();

    struct GridPos
    {
        int col = 0;
        int row = 0;
    };

    GridPos pixel_to_cell(int px, int py) const;
    bool open_link_at(const GridPos& pos, ModifierFlags mod);

    // Surface-layer prefixes. Concrete hosts call these first from on_key /
    // on_text_input / dispatch_action and return early when the surface
    // consumed the event (copy mode swallow, selection-copy shortcut,
    // post-copy text suppression, copy/toggle_copy_mode actions).
    bool handle_surface_key(const KeyEvent& event);
    bool handle_surface_text_input(const TextInputEvent& event);
    bool handle_surface_action(std::string_view action);

    bool copy_active_selection_to_clipboard();

    bool copy_mode_active() const
    {
        return copy_mode_.active;
    }

    // Drop selection state without yanking — used when the content underneath
    // the grid-anchored selection is about to shift (scroll, new output,
    // snapshot replacement, resize).
    void clear_surface_selection();

    // After the grid content or dimensions changed externally (e.g. a remote
    // snapshot was applied), keep the copy-mode cursor inside the grid and
    // refresh its overlay. No-op while copy mode is inactive.
    void refresh_copy_mode_after_grid_change();

    SelectionManager& selection()
    {
        return selection_;
    }
    const SelectionManager& selection() const
    {
        return selection_;
    }
    MouseReporter& mouse_reporter()
    {
        return mouse_reporter_;
    }

    // --- host-specific seams ------------------------------------------------
    // Whether this client may forward mouse reports to the terminal process
    // (remote observers may not; selection handling then takes over).
    virtual bool surface_mouse_reporting_allowed() const
    {
        return true;
    }
    // Deliver an encoded mouse-report sequence to the terminal process.
    virtual void surface_write_mouse_report(std::string_view sequence) = 0;
    // Scroll the surface view by `rows` (positive scrolls up into history,
    // negative back toward live). Hosts without a history view may no-op.
    virtual void surface_scroll_lines(int rows) = 0;
    // Terminal cursor position used as the copy-mode starting point.
    virtual glm::ivec2 surface_cursor_position() const = 0;

private:
    // Vim/tmux-style keyboard copy mode. When active, key events navigate a
    // separate copy-mode cursor instead of being forwarded to the underlying
    // terminal process. The cursor is rendered as a single-cell selection
    // overlay so the user can see where they are.
    struct CopyMode
    {
        bool active = false;
        bool selecting = false;
        bool line_mode = false;
        glm::ivec2 cursor{ 0, 0 };
        glm::ivec2 anchor{ 0, 0 };
    };

    void enter_copy_mode();
    void exit_copy_mode(bool yank);
    bool handle_copy_mode_key(const KeyEvent& event);
    void update_copy_mode_overlay();

    MouseReporter mouse_reporter_;
    SelectionManager selection_;
    CopyMode copy_mode_;
    std::optional<GridPos> pending_selection_copy_click_;
    bool suppress_next_selection_copy_text_input_ = false;
};

} // namespace draxul
