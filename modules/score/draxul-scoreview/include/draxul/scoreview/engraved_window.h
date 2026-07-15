#pragma once

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/layout_engine.h>
#include <draxul/scoreview/score_draw_list.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace draxul
{
namespace scoreview
{

// A note falling through the waterfall (piano-roll) band: its onset/duration in
// quarter notes, its MIDI pitch (the target key), and its spelling-palette
// index. Built from the timemap's on/off pairs during engraving. Hoisted to
// namespace scope so the background engraver can produce it off the main thread.
struct WaterfallNote
{
    double onset_q = 0.0;
    double duration_q = 0.0;
    int midi = -1;
    int palette = 0;
};

// Everything a rolling-window engrave produces, self-contained so it can be
// built off the main thread and swapped into the host in one cheap move. The
// FlowController is a pure value (move-assignable), so `flow_ = std::move(...)`
// installs the new transport without disturbing anything holding &flow_.
struct EngravedWindow
{
    std::shared_ptr<const ScoreDrawList> strip;
    FlowController flow;
    std::unordered_map<std::string, int> palette; // note id -> palette index
    std::vector<WaterfallNote> waterfall;
};

// Inputs an engrave needs beyond the window's MusicXML. `marking_qpm` is the
// piece's true tempo marking (a windowed slice loses the bar-1 tempo text, so
// the host supplies it); `lock_tempo` holds that marking with no Roll easing;
// `proportional_spacing` is the constant-scroll spacing experiment (see
// LayoutOptions).
struct EngraveParams
{
    float pixel_scale = 1.0f;
    double marking_qpm = 0.0;
    bool lock_tempo = false;
    bool proportional_spacing = false;
};

enum class EngraveResult : uint8_t
{
    InterpretFailed, // the SVG could not be interpreted into a draw list
    TransportFailed, // the timemap yielded no usable onsets (strip still set)
    Ok,
};

// Extracts the draw strip, a freshly-built FlowController (build + gates), the
// spelling palette, and the waterfall from an ALREADY-LOADED engine. Sets the
// engine's Flow layout options. Pure with respect to host state — it only
// touches `engine` and writes `out` — so it is safe to run on a worker thread
// against that thread's own engine. Does NOT set transport mode/marking/tempo;
// that is install policy (see engrave_window / ScoreHost::install_window).
// On TransportFailed `out.strip` is still populated (the caller may show it).
EngraveResult engrave_loaded(
    ILayoutEngine& engine, const EngraveParams& params, EngravedWindow& out, std::string& error);

// Loads `window_xml` into `engine`, then engraves a ready-to-install Roll
// window: engrave_loaded plus set_mode(Roll), the supplied marking, and the
// tempo lock. Shared by the synchronous rebuild and the async worker.
EngraveResult engrave_window(ILayoutEngine& engine, const std::string& window_xml,
    const EngraveParams& params, EngravedWindow& out, std::string& error);

} // namespace scoreview
} // namespace draxul
