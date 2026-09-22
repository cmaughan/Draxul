#include <draxul/gui/overlay_text.h>
#include <draxul/gui/palette_renderer.h>
#include <draxul/gui/toast_renderer.h>
#include <draxul/gui/tooltip.h>
#include <draxul/text_service.h>

int main()
{
    draxul::TextService text;
    const draxul::gui::PaletteViewState state{};
    const auto cells = draxul::gui::render_palette(state, text);
    return cells.empty() && draxul::gui::overlay_text_width("render contract") == 15
        ? 0
        : 1;
}
