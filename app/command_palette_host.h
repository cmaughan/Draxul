#pragma once

#include "command_palette.h"
#include <draxul/host.h>
#include <draxul/renderer.h>
#include <memory>

namespace draxul
{

class GuiActionHandler;
class TextService;
struct GuiKeybinding;

/// IHost implementation that wraps CommandPalette as a full-window overlay.
/// Drawn last by App so it appears on top of all other hosts. Uses a regular
/// IGridHandle and produces CellUpdate vectors via gui::render_palette().
class CommandPaletteHost : public IHost
{
public:
    struct Deps
    {
        GuiActionHandler* gui_action_handler = nullptr;
        const std::vector<GuiKeybinding>* keybindings = nullptr;
        float* palette_bg_alpha = nullptr;
    };

    explicit CommandPaletteHost(Deps deps);

    // IHost interface
    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    void set_viewport(const HostViewport& viewport) override;
    void pump() override;
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;

    bool open_prompt(CommandPalette::PromptRequest request);
    bool open_choices(CommandPalette::ChoiceRequest request);
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

    bool is_active() const;

private:
    bool ensure_palette_handle();
    void refresh_open_palette();
    PaneDescriptor palette_pane_descriptor() const;

    Deps deps_;
    CommandPalette palette_;
    std::unique_ptr<IGridHandle> handle_;
    IHostCallbacks* callbacks_ = nullptr;
    IGridRenderer* renderer_ = nullptr;
    TextService* text_service_ = nullptr;
    int pixel_x_ = 0;
    int pixel_y_ = 0;
    int pixel_w_ = 0;
    int pixel_h_ = 0;
};

} // namespace draxul
