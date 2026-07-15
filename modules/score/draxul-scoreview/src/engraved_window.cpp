#include <draxul/scoreview/engraved_window.h>

#include <draxul/scoreview/keyboard_render_nvg.h>
#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/svg_score_interpreter.h>

#include <draxul/log.h>

#include <algorithm>
#include <utility>

namespace draxul
{
namespace scoreview
{

EngraveResult engrave_loaded(
    ILayoutEngine& engine, const EngraveParams& params, EngravedWindow& out, std::string& error)
{
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    options.pixel_scale = params.pixel_scale;
    options.proportional_spacing = params.proportional_spacing;
    engine.set_options(options);

    auto strip = interpret_score_svg(engine.render_page_svg(1), error);
    if (!strip)
        return EngraveResult::InterpretFailed;
    for (const std::string& warning : strip->warnings)
        DRAXUL_LOG_DEBUG(LogCategory::App, "score flow interpreter: %s", warning.c_str());

    // Keep the strip even if the transport join fails: a failed window can
    // still fall back to showing the engraving.
    out.strip = std::make_shared<const ScoreDrawList>(std::move(*strip));
    auto timemap = parse_timemap(engine.render_timemap(), error);
    if (!timemap.has_value() || !out.flow.build(*timemap, *out.strip, error))
        return EngraveResult::TransportFailed;

    // Expected notes + tie continuations for the gate (same id space).
    out.flow.prepare_gates(
        [&engine](const std::string& id) { return engine.midi_pitch_for_element(id); },
        engine.tie_end_ids());

    // Whole-window coloring: resolve each note's spelling once (the engine
    // knows its notated letter here) into the pairing palette.
    out.palette.clear();
    for (const FlowController::Gate& gate : out.flow.gates())
    {
        for (const FlowController::GateNote& note : gate.notes)
        {
            if (note.pitch < 0)
                continue;
            const int letter = engine.note_letter_for_element(note.id);
            out.palette[note.id] = guidance_palette_index(note.pitch, letter);
        }
    }

    // Waterfall blocks: pair each timemap note_on with its note_off for a
    // duration, colored by the same spelling palette as the sheet.
    out.waterfall.clear();
    const auto note_for = [&](const std::string& id, double onset, double off) {
        const int midi = engine.midi_pitch_for_element(id);
        WaterfallNote note;
        note.onset_q = onset;
        note.duration_q = std::max(0.05, off - onset);
        note.midi = midi;
        const auto pal = out.palette.find(id);
        note.palette = pal != out.palette.end() ? pal->second : guidance_palette_index(midi);
        return std::make_pair(note, midi >= 0);
    };
    std::unordered_map<std::string, double> open; // id -> onset q
    for (const TimemapEntry& entry : timemap->entries)
    {
        for (const std::string& id : entry.note_off)
        {
            auto it = open.find(id);
            if (it == open.end())
                continue;
            auto [note, ok] = note_for(id, it->second, entry.qstamp);
            if (ok)
                out.waterfall.push_back(note);
            open.erase(it);
        }
        for (const std::string& id : entry.note_on)
            open[id] = entry.qstamp;
    }
    // Notes without an explicit off (final ties) run to the piece end.
    for (const auto& [id, onset] : open)
    {
        auto [note, ok] = note_for(id, onset, timemap->duration_q);
        if (ok)
            out.waterfall.push_back(note);
    }
    return EngraveResult::Ok;
}

EngraveResult engrave_window(ILayoutEngine& engine, const std::string& window_xml,
    const EngraveParams& params, EngravedWindow& out, std::string& error)
{
    if (window_xml.empty() || !engine.load(window_xml, error))
        return EngraveResult::InterpretFailed;

    const EngraveResult result = engrave_loaded(engine, params, out, error);
    if (result != EngraveResult::Ok)
        return result;

    // Roll transport policy for a rolling window: the piece's marking (the
    // slice lost the bar-1 tempo text), no adaptation while locked.
    out.flow.set_marking_qpm(params.marking_qpm);
    out.flow.set_mode(FlowController::TransportMode::Roll);
    out.flow.set_adapt_tempo(!params.lock_tempo);
    if (params.lock_tempo && params.marking_qpm > 0.0)
        out.flow.set_tempo_qpm(params.marking_qpm);
    return result;
}

} // namespace scoreview
} // namespace draxul
