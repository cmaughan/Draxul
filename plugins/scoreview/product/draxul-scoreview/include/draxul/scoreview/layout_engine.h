#pragma once

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace draxul
{
namespace scoreview
{

enum class LayoutMode : uint8_t
{
    // Fit pages to page_size_px (the reading view).
    Paged,
    // One endless system (Verovio breaks:none) — the conveyor strip for the
    // flowing runner view (plans/scoreview-conveyor.md). Page size is
    // ignored; the strip's canvas is resolution-independent and the host
    // chooses its on-screen height at draw time.
    Flow,
};

// Verovio spacing presets: width ~ spacingLinear * duration^spacingNonLinear.
// Authentic = Verovio's engraving defaults. Proportional = space linear in
// duration for a near-constant conveyor scroll, with the width multiplier
// compressed so bars stay a sane size (tuned on the Grieg — strictly
// proportional at 0.25 made one bar fill the screen; glyph minimums floor
// the compression). Shared by the engine's option emission and the
// inspector's spacing-debug sliders.
constexpr float kSpacingLinearDefault = 0.25f;
constexpr float kSpacingNonLinearDefault = 0.6f;
constexpr float kSpacingLinearProportional = 0.06f;
constexpr float kSpacingNonLinearProportional = 1.0f;

// Layout parameters for engraving. page_size_px is the target page size in
// device pixels (Paged mode); pixel_scale is the display's device-pixel
// ratio. Staff and glyph sizes scale with pixel_scale so a retina page isn't
// half-size music.
struct LayoutOptions
{
    LayoutMode mode = LayoutMode::Paged;
    glm::ivec2 page_size_px{ 840, 1188 }; // A4 aspect at ~96 dpi
    float pixel_scale = 1.0f;
    // Flow-mode experiment: space notes proportionally to their duration
    // (Verovio spacingNonLinear 1.0 instead of the engraver's 0.6 curve) so
    // the conveyor scrolls at near-constant speed and score columns line up
    // with the time-proportional waterfall. Ignored in Paged mode — the
    // reading view always keeps authentic engraving spacing.
    bool proportional_spacing = false;
    // Density override for Verovio's spacingLinear (the per-duration width
    // multiplier). Negative = the mode's default: 0.25 (Verovio's) normally,
    // a compressed constant in proportional mode — strict space-per-duration
    // stretches long notes so far that a bar fills the screen, so
    // proportional mode trades a little flatness for authentic-like density.
    float spacing_linear = -1.0f;
    // Curve override for Verovio's spacingNonLinear (space ~ duration^value,
    // engraver's default 0.6, proportional 1.0). Negative = the preset picked
    // by proportional_spacing. Both overrides exist for the inspector's
    // spacing-debug sliders and the tuning probe.
    float spacing_non_linear = -1.0f;
};

// Boundary between the score pipeline and the engraving engine (Verovio
// today; a custom piano-scoped engine is a possible future swap —
// plans/scoreview.md). Input is score bytes; output is one SVG string per
// page in the engine's constrained SVG dialect, consumed by the phase-3
// interpreter.
class ILayoutEngine
{
public:
    virtual ~ILayoutEngine() = default;

    // Loads .musicxml (uncompressed XML) or .mxl (zip container) bytes and
    // performs the initial layout. Returns false and fills `error` on failure.
    virtual bool load(std::string_view bytes, std::string& error) = 0;

    // Applies new layout options; re-layouts already-loaded music.
    virtual void set_options(const LayoutOptions& options) = 0;

    virtual bool is_loaded() const = 0;
    virtual int page_count() = 0;

    // Renders one page (1-based) to SVG. Returns an empty string for an
    // invalid page or when nothing is loaded.
    virtual std::string render_page_svg(int page_number) = 0;

    // Renders the loaded piece's timemap as JSON (an array of
    // {qstamp, tstamp, on[], off[], tempo?} entries in playback order).
    // Returns an empty string when nothing is loaded.
    virtual std::string render_timemap() = 0;

    // MIDI pitch (1-127) for a note element id, or -1 when unknown. Same id
    // space as the timemap and draw list — the gate's expected notes need no
    // model bridge (plans/scoreview-gate.md).
    virtual int midi_pitch_for_element(const std::string& element_id) = 0;

    // Notated diatonic letter (0=C .. 6=B) for a note element id, or -1 when
    // unknown. With midi_pitch_for_element this recovers the written spelling
    // — C# and Db share a MIDI pitch but differ by letter — which the pairing
    // palette colors distinctly.
    virtual int note_letter_for_element(const std::string& element_id) = 0;
    // The engraved staff carrying this note (1 = upper/right hand on a grand
    // staff, 2 = lower/left), 0 = unknown. Defaulted so layout fakes that
    // predate hand attribution keep compiling.
    virtual int staff_for_element(const std::string& element_id)
    {
        (void)element_id;
        return 0;
    }

    // Element ids that END a tie (the continuation notes). Their gates
    // auto-open in the runner: demanding a re-strike would be musically
    // wrong. Empty when nothing is loaded.
    virtual std::vector<std::string> tie_end_ids() = 0;
};

} // namespace scoreview
} // namespace draxul
