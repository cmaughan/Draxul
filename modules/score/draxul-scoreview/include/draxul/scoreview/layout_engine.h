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

// Layout parameters for engraving. page_size_px is the target page size in
// device pixels (Paged mode); pixel_scale is the display's device-pixel
// ratio. Staff and glyph sizes scale with pixel_scale so a retina page isn't
// half-size music.
struct LayoutOptions
{
    LayoutMode mode = LayoutMode::Paged;
    glm::ivec2 page_size_px{ 840, 1188 }; // A4 aspect at ~96 dpi
    float pixel_scale = 1.0f;
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

    // Element ids that END a tie (the continuation notes). Their gates
    // auto-open in the runner: demanding a re-strike would be musically
    // wrong. Empty when nothing is loaded.
    virtual std::vector<std::string> tie_end_ids() = 0;
};

} // namespace scoreview
} // namespace draxul
