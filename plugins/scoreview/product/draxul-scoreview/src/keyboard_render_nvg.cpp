#include <draxul/scoreview/keyboard_render_nvg.h>

#include "nanovg.h"

#include <algorithm>
#include <cmath>

namespace draxul
{
namespace scoreview
{

void draw_piano_keyboard(NVGcontext* vg, float x, float y, float w, float h,
    const std::vector<KeyboardLit>& lit, float overall_alpha, float pixel_scale)
{
    if (overall_alpha <= 0.01f || w <= 0.0f || h <= 0.0f)
        return;
    struct Glow
    {
        float alpha = 0.0f;
        int palette = 0;
    };
    const auto lit_glow = [&lit](int midi) {
        Glow best;
        for (const KeyboardLit& key : lit)
        {
            if (key.midi == midi && key.alpha > best.alpha)
                best = { key.alpha, key.palette };
        }
        return best;
    };

    nvgSave(vg);
    nvgGlobalAlpha(vg, overall_alpha);

    const float white_w = w / static_cast<float>(kKeyboardWhiteKeys);
    const float line = std::max(1.0f, 1.0f * pixel_scale);

    // Felt rail above the keys.
    nvgBeginPath(vg);
    nvgRect(vg, x, y - 4.0f * pixel_scale, w, 4.0f * pixel_scale);
    nvgFillColor(vg, nvgRGBA(120, 32, 36, 255));
    nvgFill(vg);

    // White keys.
    for (int midi = kKeyboardLowMidi; midi <= kKeyboardHighMidi; ++midi)
    {
        if (keyboard_is_black(midi))
            continue;
        const int index = keyboard_white_index(midi);
        const float key_x = x + static_cast<float>(index) * white_w;
        nvgBeginPath(vg);
        nvgRoundedRect(
            vg, key_x, y, white_w - line, h, 2.0f * pixel_scale);
        const Glow glow = lit_glow(midi);
        if (glow.alpha > 0.0f)
        {
            const unsigned char* tint = kGuidancePalette[glow.palette % kGuidancePaletteSize];
            nvgFillColor(vg,
                nvgRGBA(static_cast<unsigned char>(248 + (tint[0] - 248) * glow.alpha),
                    static_cast<unsigned char>(247 + (tint[1] - 247) * glow.alpha),
                    static_cast<unsigned char>(243 + (tint[2] - 243) * glow.alpha), 255));
        }
        else
        {
            nvgFillColor(vg, nvgRGBA(248, 247, 243, 255));
        }
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGBA(60, 60, 64, 200));
        nvgStrokeWidth(vg, line);
        nvgStroke(vg);
    }

    // Black keys on top.
    const float black_w = white_w * 0.62f;
    const float black_h = h * 0.62f;
    for (int midi = kKeyboardLowMidi; midi <= kKeyboardHighMidi; ++midi)
    {
        if (!keyboard_is_black(midi))
            continue;
        const float center = keyboard_key_center_x(midi, x, w);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, center - black_w * 0.5f, y, black_w, black_h,
            1.5f * pixel_scale);
        const Glow glow = lit_glow(midi);
        if (glow.alpha > 0.0f)
        {
            const unsigned char* tint = kGuidancePalette[glow.palette % kGuidancePaletteSize];
            nvgFillColor(vg,
                nvgRGBA(static_cast<unsigned char>(26 + (tint[0] - 26) * glow.alpha),
                    static_cast<unsigned char>(26 + (tint[1] - 26) * glow.alpha),
                    static_cast<unsigned char>(28 + (tint[2] - 28) * glow.alpha), 255));
        }
        else
        {
            nvgFillColor(vg, nvgRGBA(26, 26, 28, 255));
        }
        nvgFill(vg);
    }

    nvgRestore(vg);
}

} // namespace scoreview
} // namespace draxul
