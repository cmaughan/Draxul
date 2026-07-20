#include <draxul/scoreview/score_host.h>

// The ImGui debug/learning inspector (kanban 21, the ScoreViewModel seam):
// reads ONE per-frame snapshot (build_view_model) and requests every
// mutation through ScoreInspectorIntents, applied at one defined point after
// ImGui::Render — the inspector can no longer change state under the frame
// being recorded. Same class, internal linkage only.

#include <draxul/imgui_host.h>
#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/layout_engine.h>
#include <draxul/scoreview/piece_analysis.h>
#include <draxul/scoreview/player_input_rig.h>

#include "score_view_model.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

// note_name(midi) lives in draxul-score-learn (piece_analysis.h), reached here
// through score_view_model.h; the inspector no longer carries its own copy.

void ScoreHost::render_debug_ui(float dt)
{
    if (imgui_context_ == nullptr || imgui_backend_ == nullptr)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    imgui_backend_->begin_imgui_frame();
    ImGuiIO& io = ImGui::GetIO();
    const int pw = std::max(1, viewport_.pixel_size.x);
    const int ph = std::max(1, viewport_.pixel_size.y);
    io.DisplaySize = ImVec2(static_cast<float>(viewport_.pixel_pos.x + pw),
        static_cast<float>(viewport_.pixel_pos.y + ph));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);
    ImGui::NewFrame();

    const ScoreViewModel view = build_view_model();
    ScoreInspectorIntents intents;

    if (show_debug_ui_)
    {
        ImGui::SetNextWindowPos(
            ImVec2(static_cast<float>(viewport_.pixel_pos.x) + 16.0f,
                static_cast<float>(viewport_.pixel_pos.y) + 44.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(440.0f, 620.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (ImGui::Begin("ScoreView learning inspector", &show_debug_ui_))
        {
            if (ImGui::CollapsingHeader("Transport", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Button(view.playing ? "Pause" : "Play"))
                    intents.toggle_play = true;
                ImGui::SameLine();
                if (ImGui::Button("Rewind / restart"))
                    intents.rewind_or_restart = true;
                float tempo = static_cast<float>(view.tempo_qpm);
                if (ImGui::SliderFloat("tempo qpm", &tempo,
                        static_cast<float>(view.min_tempo_qpm),
                        static_cast<float>(view.max_tempo_qpm), "%.0f"))
                    intents.tempo_qpm = tempo;
                bool lock_tempo = view.lock_tempo;
                if (ImGui::Checkbox("Lock tempo (play at marking, no adapt)", &lock_tempo))
                    intents.lock_tempo = lock_tempo;
                ImGui::Text("%.0f%% of marking (%.0f qpm)",
                    view.marking_qpm > 0.0 ? view.tempo_qpm / view.marking_qpm * 100.0 : 0.0,
                    view.marking_qpm);
                const auto roll = static_cast<int>(FlowController::TransportMode::Roll);
                const auto gate = static_cast<int>(FlowController::TransportMode::Gate);
                const char* mode = view.transport_mode == roll ? "Roll"
                    : view.transport_mode == gate              ? "Gate"
                                                               : "Clock";
                ImGui::Text("mode %s   position %.2f q", mode, view.position_q);
                if (view.composing && !view.composer_name.empty())
                    ImGui::Text("composer: %s", view.composer_name.c_str());
                if (view.engraving_busy)
                    ImGui::TextDisabled("engraving latest changes...");

                // Player input source: dev keyboard / microphone / any MIDI
                // input port. Ports enumerate ONLY while the combo is open —
                // never per frame: each probe touches the CoreMIDI client,
                // and a 60Hz probe both hammers the MIDI server and turns a
                // transient server failure into a per-frame retry storm.
                // Switching swaps the input seam live — verdicts, score and
                // transport survive.
                std::string current = "Dev keyboard";
                if (view.input_kind == PlayerInputRig::Kind::Mic)
                    current = "Microphone";
                else if (view.input_kind == PlayerInputRig::Kind::Midi)
                    current = "MIDI: " + view.midi_port_name;
                if (ImGui::BeginCombo("input", current.c_str()))
                {
                    const std::vector<std::string> midi_ports
                        = PlayerInputRig::list_midi_ports();
                    if (ImGui::Selectable("Dev keyboard",
                            view.input_kind == PlayerInputRig::Kind::Keyboard))
                        intents.select_keyboard_input = true;
                    if (ImGui::Selectable(
                            "Microphone", view.input_kind == PlayerInputRig::Kind::Mic))
                        intents.select_mic_input = true;
                    for (int port = 0; port < static_cast<int>(midi_ports.size()); ++port)
                    {
                        const std::string label
                            = "MIDI: " + midi_ports[static_cast<size_t>(port)];
                        const bool active
                            = view.midi_port_name == midi_ports[static_cast<size_t>(port)];
                        if (ImGui::Selectable(label.c_str(), active))
                            intents.select_midi_port = port;
                    }
                    if (midi_ports.empty())
                        ImGui::TextDisabled("(no MIDI inputs found)");
                    ImGui::EndCombo();
                }
            }

            if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float score_pct = view.score_height_frac * 100.0f;
                if (ImGui::SliderFloat("score height %", &score_pct, 20.0f, 60.0f, "%.0f"))
                    intents.score_height_frac = score_pct / 100.0f;
                bool waterfall = view.show_waterfall;
                if (ImGui::Checkbox("Waterfall", &waterfall))
                    intents.show_waterfall = waterfall;
                float beats = static_cast<float>(view.waterfall_beats);
                if (ImGui::SliderFloat("look-ahead beats", &beats, 3.0f, 16.0f, "%.1f"))
                    intents.waterfall_beats = beats;
                float gate = static_cast<float>(view.note_gate);
                if (ImGui::SliderFloat("articulation", &gate, 0.10f, 1.0f, "%.2f"))
                    intents.note_gate = gate;
                ImGui::SameLine();
                ImGui::TextDisabled("(staccato..legato)");
                bool mark = view.mark_mistakes;
                if (ImGui::Checkbox("Mark wrong notes (x)", &mark))
                    intents.mark_mistakes = mark;
                bool colors = view.show_note_colors;
                if (ImGui::Checkbox("Note colors (full score)", &colors))
                    intents.show_note_colors = colors;
                bool split = view.split_accidentals;
                if (ImGui::Checkbox("Split sharps/flats (half color)", &split))
                    intents.split_accidentals = split;
                ImGui::SameLine();
                ImGui::TextDisabled("off = full color");
                bool composer = view.composer_enabled;
                if (ImGui::Checkbox("Composer (adaptive)", &composer))
                    intents.composer_enabled = composer;
                ImGui::Indent();
                bool drills = view.composer_drills;
                if (ImGui::Checkbox("Chord drills", &drills))
                    intents.composer_drills = drills;
                ImGui::SameLine();
                ImGui::TextDisabled("(fabricated grabs)");
                bool scales = view.composer_scales;
                if (ImGui::Checkbox("Scale drills", &scales))
                    intents.composer_scales = scales;
                ImGui::SameLine();
                ImGui::TextDisabled("(troubled registers)");
                ImGui::Unindent();
                bool proportional = view.proportional_spacing;
                if (ImGui::Checkbox("Proportional spacing", &proportional))
                    intents.proportional_spacing = proportional;
                ImGui::SameLine();
                ImGui::TextDisabled("(constant scroll)");
                bool analysis = view.show_analysis_overlay;
                if (ImGui::Checkbox("Analysis overlay ('a' in reading view)", &analysis))
                    intents.show_analysis_overlay = analysis;
                ImGui::SameLine();
                ImGui::TextDisabled("(green = discovered)");
                bool unique = view.show_unique_chunks;
                if (ImGui::Checkbox("Unique chunks ('s' in reading view)", &unique))
                    intents.show_unique_chunks = unique;
                ImGui::SameLine();
                ImGui::TextDisabled("(ghost the repeats)");
            }

            if (ImGui::CollapsingHeader("Spacing debug"))
            {
                // Live Verovio spacing knobs (flow view only). Sliders edit a
                // copy and apply ON RELEASE — every apply is a full ~100ms
                // re-engrave, so never per drag frame.
                const float preset_linear = view.proportional_spacing
                    ? kSpacingLinearProportional
                    : kSpacingLinearDefault;
                const float preset_non_linear = view.proportional_spacing
                    ? kSpacingNonLinearProportional
                    : kSpacingNonLinearDefault;
                float linear = view.spacing_linear_override >= 0.0f
                    ? view.spacing_linear_override
                    : preset_linear;
                float non_linear = view.spacing_non_linear_override >= 0.0f
                    ? view.spacing_non_linear_override
                    : preset_non_linear;
                ImGui::SliderFloat("spacingLinear", &linear, 0.01f, 0.5f, "%.3f",
                    ImGuiSliderFlags_Logarithmic);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    intents.spacing_linear_override = linear;
                ImGui::SliderFloat("spacingNonLinear", &non_linear, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
                    intents.spacing_non_linear_override = non_linear;
                ImGui::TextDisabled("width ~ spacingLinear * duration^spacingNonLinear");
                const bool overriding = view.spacing_linear_override >= 0.0f
                    || view.spacing_non_linear_override >= 0.0f;
                if (overriding)
                {
                    ImGui::Text("overriding the preset");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset to preset"))
                        intents.reset_spacing_overrides = true;
                }
                else
                {
                    ImGui::TextDisabled("at preset (%s)",
                        view.proportional_spacing ? "proportional" : "authentic");
                }
            }

            if (ImGui::CollapsingHeader("Audio"))
            {
                int level = view.tick_level;
                const char* levels[] = { "Off", "Beats", "Eighths" };
                if (ImGui::Combo("metronome", &level, levels, 3))
                    intents.tick_level = level;
                // The instrument voicing audition: the built-in synth or
                // any staged .sf2 (loaded on selection).
                const auto& soundfonts = *view.soundfonts;
                std::string instrument_label = "Synth (3-partial)";
                const int selected = view.loaded_soundfont_index >= 0
                    ? view.loaded_soundfont_index
                    : view.selected_soundfont_index;
                if (view.piano_voice && selected >= 0
                    && selected < static_cast<int>(soundfonts.size()))
                    instrument_label
                        = soundfonts[static_cast<size_t>(selected)].stem().string();
                if (ImGui::BeginCombo("instrument", instrument_label.c_str()))
                {
                    if (ImGui::Selectable("Synth (3-partial)", !view.piano_voice))
                        intents.use_synth_voice = true;
                    for (int i = 0; i < static_cast<int>(soundfonts.size()); ++i)
                    {
                        const std::string name
                            = soundfonts[static_cast<size_t>(i)].stem().string();
                        const bool active = view.piano_voice && selected == i;
                        if (ImGui::Selectable(name.c_str(), active))
                            intents.use_piano_index = i;
                    }
                    if (soundfonts.empty())
                        ImGui::TextDisabled("(no .sf2 in soundfonts/)");
                    ImGui::EndCombo();
                }
                bool audition = view.audition;
                if (ImGui::Checkbox("Audition (hear notes)", &audition))
                    intents.audition = audition;
            }

            if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("accuracy EMA  %.0f%%", view.accuracy_ema * 100.0);
                ImGui::Text("score %d   streak %d", view.score, view.streak);
                ImGui::Text("misses %d   wrong notes %d", view.miss_count, view.wrong_count);
                ImGui::Text("notes judged (lifetime) %d", view.model->total_notes_judged());
                ImGui::Text("key %s (conf %.2f)   %zu chords, %zu figures",
                    key_name(view.profile->global_key.tonic_pc, view.profile->global_key.minor)
                        .c_str(),
                    view.profile->global_key.confidence, view.profile->chords.size(),
                    view.profile->figures.size());
                if (ImGui::Button("Clear progress + restart"))
                    intents.clear_progress = true;
                ImGui::SameLine();
                ImGui::TextDisabled("wipes this piece's record");
            }

            if (ImGui::CollapsingHeader("Timing drift", ImGuiTreeNodeFlags_DefaultOpen))
            {
                double sum = 0.0;
                int n = 0;
                for (const auto& [q, s] : view.model->onset_stats())
                {
                    sum += s.timing.mean_q * s.timing.samples;
                    n += s.timing.samples;
                }
                const double mean = n > 0 ? sum / n : 0.0;
                ImGui::Text("overall %+.3f beats  (%s)", mean,
                    mean > 0.02 ? "dragging" : mean < -0.02 ? "rushing"
                                                            : "on time");
                struct Drift
                {
                    double absmean, q, mean;
                };
                std::vector<Drift> drift;
                for (const auto& [q, s] : view.model->onset_stats())
                    if (s.timing.samples >= 2)
                        drift.push_back({ std::abs(s.timing.mean_q), q, s.timing.mean_q });
                std::sort(drift.begin(), drift.end(),
                    [](const Drift& a, const Drift& b) { return a.absmean > b.absmean; });
                if (drift.empty())
                    ImGui::TextDisabled("(no timed onsets yet)");
                for (size_t i = 0; i < drift.size() && i < 5; ++i)
                    ImGui::BulletText("q %.1f  %+.3f beats", drift[i].q, drift[i].mean);
            }

            if (ImGui::CollapsingHeader("Trouble spots", ImGuiTreeNodeFlags_DefaultOpen))
            {
                // Bars: worst by wrong count, each expandable to the two hands
                // (ok = right notes, x = wrong; the hand split is a heuristic
                // around middle C).
                if (ImGui::TreeNodeEx("Bars", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    std::vector<std::pair<int, int>> bars; // wrong, bar
                    for (const auto& [bar, t] : view.model->bar_tally())
                        if (t.miss > 0 || t.hit > 0)
                            bars.emplace_back(t.miss, bar);
                    std::sort(bars.rbegin(), bars.rend());
                    if (bars.empty())
                        ImGui::TextDisabled("(none yet)");
                    for (size_t i = 0; i < bars.size() && i < 16; ++i)
                    {
                        const int bar = bars[i].second;
                        const PlayerModel::BarTally& t = view.model->bar_tally().at(bar);
                        if (ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(bar)),
                                "bar %d    %d ok / %d wrong", bar + 1, t.hit, t.miss))
                        {
                            ImGui::BulletText(
                                "left hand    %d ok / %d wrong", t.left.hit, t.left.miss);
                            ImGui::BulletText(
                                "right hand   %d ok / %d wrong", t.right.hit, t.right.miss);
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Chords (net trouble)"))
                {
                    std::vector<std::pair<int, std::string>> chords;
                    for (const auto& [key, s] : view.model->chord_stats())
                    {
                        const int trouble = s.miss + s.split - s.clean;
                        if (trouble > 0)
                            chords.emplace_back(trouble, key);
                    }
                    std::sort(chords.rbegin(), chords.rend());
                    if (chords.empty())
                        ImGui::TextDisabled("(none yet)");
                    for (size_t i = 0; i < chords.size() && i < 8; ++i)
                        ImGui::BulletText(
                            "%s   (%d)", chords[i].second.c_str(), chords[i].first);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Pitches (missed)"))
                {
                    std::vector<std::pair<int, int>> pitches; // miss, midi
                    for (const auto& [midi, s] : view.model->pitch_stats())
                        if (s.miss > 0)
                            pitches.emplace_back(s.miss, midi);
                    std::sort(pitches.rbegin(), pitches.rend());
                    if (pitches.empty())
                        ImGui::TextDisabled("(none yet)");
                    for (size_t i = 0; i < pitches.size() && i < 8; ++i)
                        ImGui::BulletText("%s   (missed %d)",
                            note_name(pitches[i].second).c_str(), pitches[i].first);
                    ImGui::TreePop();
                }
            }

            if (view.composing
                && ImGui::CollapsingHeader("Composer program", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const int slot_now = view.program->slot_at(view.stream_position_q);
                const int planned = view.program->size();
                for (int s = slot_now; s < planned && s < slot_now + 10; ++s)
                {
                    const StreamBarPlan& p = view.program->plan(s);
                    const char* kind = p.kind == StreamBarPlan::Kind::Piece ? "piece"
                        : p.kind == StreamBarPlan::Kind::Review             ? "review"
                                                                            : "drill";
                    if (!p.reason.empty())
                        ImGui::Text("%s %2d  %s", s == slot_now ? ">" : " ", s, p.reason.c_str());
                    else
                        ImGui::Text("%s %2d  %-6s bar %d", s == slot_now ? ">" : " ", s, kind,
                            p.source_bar + 1);
                }
            }

            if (ImGui::CollapsingHeader("Bar mastery"))
            {
                const int total = view.bar_count;
                int encountered = 0;
                int mastered = 0;
                for (int b = 0; b < total; ++b)
                    if (view.model->bar_encounters(b) > 0)
                    {
                        ++encountered;
                        if (view.model->bar_mastery(b) >= 0.7)
                            ++mastered;
                    }
                ImGui::Text("%d/%d bars encountered, %d mastered (>=70%%)", encountered, total,
                    mastered);
            }

            ImGui::Separator();
            ImGui::TextDisabled("` toggles this panel");
        }
        ImGui::End();
    }

    ImGui::Render();
    // The frame is fully recorded (NanoVG callback set, ImGui draw data
    // built) — the one safe point to apply what the inspector asked for.
    apply_inspector_intents(intents);
}

} // namespace scoreview
} // namespace draxul
