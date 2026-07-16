#include <draxul/scoreview/score_host.h>

// The ImGui debug/learning inspector (kanban 21): transport + view controls
// and live readouts of the player model, composer program, and piece
// analysis. Split into its own translation unit so score_host.cpp holds
// orchestration, not a 400-line panel; same class, internal linkage only.

#include <draxul/imgui_host.h>
#include <draxul/log.h>

#include "score_audio_controller.h"
#include "score_session_controller.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

namespace
{
std::string note_name(int midi)
{
    static const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A",
        "A#", "B" };
    if (midi < 0)
        return "?";
    return std::string(kNames[((midi % 12) + 12) % 12]) + std::to_string(midi / 12 - 1);
}
} // namespace

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
                const bool playing = flow_.playing();
                if (ImGui::Button(playing ? "Pause" : "Play"))
                {
                    if (playing)
                        flow_.pause();
                    else
                        flow_.play();
                }
                ImGui::SameLine();
                if (ImGui::Button("Rewind / restart"))
                {
                    if (stream_active() && flow_.mode() == FlowController::TransportMode::Roll)
                    {
                        restart_stream(/*keep_tempo=*/true);
                    }
                    else
                    {
                        flow_.rewind();
                        apply_lit_update();
                    }
                }
                float tempo = static_cast<float>(flow_.tempo_qpm());
                if (ImGui::SliderFloat("tempo qpm", &tempo,
                        static_cast<float>(flow_.min_tempo_qpm()),
                        static_cast<float>(flow_.max_tempo_qpm()), "%.0f"))
                    flow_.set_tempo_qpm(tempo);
                if (ImGui::Checkbox("Lock tempo (play at marking, no adapt)", &lock_tempo_))
                {
                    flow_.set_adapt_tempo(!lock_tempo_);
                    if (lock_tempo_ && flow_.marking_qpm() > 0.0)
                        flow_.set_tempo_qpm(flow_.marking_qpm());
                }
                const double marking = flow_.marking_qpm();
                ImGui::Text("%.0f%% of marking (%.0f qpm)",
                    marking > 0.0 ? flow_.tempo_qpm() / marking * 100.0 : 0.0, marking);
                const char* mode = flow_.mode() == FlowController::TransportMode::Roll ? "Roll"
                    : flow_.mode() == FlowController::TransportMode::Gate              ? "Gate"
                                                                                       : "Clock";
                ImGui::Text("mode %s   position %.2f q", mode, flow_.position_q());
                if (async_engrave_in_flight_ || (engraver_ && engraver_->busy()))
                    ImGui::TextDisabled("engraving latest changes...");

                // Player input source: dev keyboard / microphone / any MIDI
                // input port. Ports enumerate ONLY while the combo is open —
                // never per frame: each probe touches the CoreMIDI client,
                // and a 60Hz probe both hammers the MIDI server and turns a
                // transient server failure into a per-frame retry storm.
                // Switching swaps the input seam live — verdicts, score and
                // transport survive.
                std::string current = "Dev keyboard";
                if (input_rig_.kind() == PlayerInputRig::Kind::Mic)
                    current = "Microphone";
                else if (input_rig_.kind() == PlayerInputRig::Kind::Midi)
                    current = "MIDI: " + input_rig_.midi_port_name();
                if (ImGui::BeginCombo("input", current.c_str()))
                {
                    const std::vector<std::string> midi_ports
                        = PlayerInputRig::list_midi_ports();
                    if (ImGui::Selectable("Dev keyboard",
                            input_rig_.kind() == PlayerInputRig::Kind::Keyboard))
                    {
                        gate_input_requested_ = GateInput::Keyboard;
                        set_gate_input(GateInput::Keyboard, 0.0, 1.0);
                    }
                    if (ImGui::Selectable(
                            "Microphone", input_rig_.kind() == PlayerInputRig::Kind::Mic))
                    {
                        gate_input_requested_ = GateInput::Mic;
                        set_gate_input(GateInput::Mic, 0.0, 1.0);
                    }
                    for (int port = 0; port < static_cast<int>(midi_ports.size()); ++port)
                    {
                        const std::string label = "MIDI: " + midi_ports[static_cast<size_t>(port)];
                        const bool active = input_rig_.midi_port_name()
                            == midi_ports[static_cast<size_t>(port)];
                        if (ImGui::Selectable(label.c_str(), active))
                        {
                            gate_input_requested_ = GateInput::Midi;
                            midi_port_requested_ = port;
                            if (set_gate_input(GateInput::Midi, 0.0, 1.0, port)
                                && !flow_.playing())
                                flow_.play(); // the piano is the interface
                        }
                    }
                    if (midi_ports.empty())
                        ImGui::TextDisabled("(no MIDI inputs found)");
                    ImGui::EndCombo();
                }
            }

            if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float score_pct = score_height_frac_ * 100.0f;
                if (ImGui::SliderFloat("score height %", &score_pct, 20.0f, 60.0f, "%.0f"))
                    score_height_frac_ = std::clamp(score_pct / 100.0f, 0.2f, 0.6f);
                if (ImGui::Checkbox("Waterfall", &show_waterfall_))
                    stream_scale_ = 0.0f; // re-fit the score band to the new layout
                float beats = static_cast<float>(waterfall_beats_);
                if (ImGui::SliderFloat("look-ahead beats", &beats, 3.0f, 16.0f, "%.1f"))
                    waterfall_beats_ = beats;
                float gate = static_cast<float>(note_gate_);
                if (ImGui::SliderFloat("articulation", &gate, 0.10f, 1.0f, "%.2f"))
                    note_gate_ = gate;
                ImGui::SameLine();
                ImGui::TextDisabled("(staccato..legato)");
                if (ImGui::Checkbox("Mark wrong notes (x)", &mark_mistakes_) && !mark_mistakes_)
                    highlight_.clear_lit(); // drop the current crosses at once
                ImGui::Checkbox("Split sharps/flats (half color)", &split_accidentals_);
                ImGui::SameLine();
                ImGui::TextDisabled("off = full color");
                if (ImGui::Checkbox("Composer (adaptive)", &composer_enabled_))
                {
                    // Re-evaluate and rebuild the stream from the top: on = the
                    // adaptive program, off = the piece scrolling unchanged.
                    // The restart keeps the player's decided tempo — switching
                    // the program is not a reason to change their pace.
                    composing_ = composer_enabled_ && stream_windowed_
                        && composer_->supports(slicer_);
                    if (composing_)
                        composer_->configure(&slicer_, &session_->model(), &session_->piece_profile());
                    restart_stream(/*keep_tempo=*/true);
                }
                // Switching the preset drops any debug overrides — the point
                // of the checkbox is the two tuned presets. The 'f' paged
                // reading view is untouched — always authentic spacing.
                if (ImGui::Checkbox("Proportional spacing", &proportional_spacing_))
                {
                    spacing_linear_override_ = -1.0f;
                    spacing_non_linear_override_ = -1.0f;
                    reengrave_flow_in_place();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(constant scroll)");
            }

            if (ImGui::CollapsingHeader("Spacing debug"))
            {
                // Live Verovio spacing knobs (flow view only). Sliders edit a
                // copy and apply ON RELEASE — every apply is a full ~100ms
                // re-engrave, so never per drag frame.
                const float preset_linear = proportional_spacing_
                    ? kSpacingLinearProportional
                    : kSpacingLinearDefault;
                const float preset_non_linear = proportional_spacing_
                    ? kSpacingNonLinearProportional
                    : kSpacingNonLinearDefault;
                float linear = spacing_linear_override_ >= 0.0f ? spacing_linear_override_
                                                                : preset_linear;
                float non_linear = spacing_non_linear_override_ >= 0.0f
                    ? spacing_non_linear_override_
                    : preset_non_linear;
                ImGui::SliderFloat("spacingLinear", &linear, 0.01f, 0.5f, "%.3f",
                    ImGuiSliderFlags_Logarithmic);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    spacing_linear_override_ = linear;
                    reengrave_flow_in_place();
                }
                ImGui::SliderFloat("spacingNonLinear", &non_linear, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    spacing_non_linear_override_ = non_linear;
                    reengrave_flow_in_place();
                }
                ImGui::TextDisabled("width ~ spacingLinear * duration^spacingNonLinear");
                const bool overriding = spacing_linear_override_ >= 0.0f
                    || spacing_non_linear_override_ >= 0.0f;
                if (overriding)
                {
                    ImGui::Text("overriding the preset");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset to preset"))
                    {
                        spacing_linear_override_ = -1.0f;
                        spacing_non_linear_override_ = -1.0f;
                        reengrave_flow_in_place();
                    }
                }
                else
                {
                    ImGui::TextDisabled("at preset (%s)",
                        proportional_spacing_ ? "proportional" : "authentic");
                }
            }

            if (ImGui::CollapsingHeader("Audio"))
            {
                int level = static_cast<int>(audio_->tick_level());
                const char* levels[] = { "Off", "Beats", "Eighths" };
                if (ImGui::Combo("metronome", &level, levels, 3))
                {
                    audio_->set_tick_level(static_cast<ScoreAudioController::TickLevel>(level));
                    if (audio_->tick_level() != ScoreAudioController::TickLevel::Off)
                        audio_->ensure_output_stream();
                }
                // The instrument voicing audition + MIDI play-thru: the
                // built-in synth or any staged .sf2 (loaded on selection).
                const auto& soundfonts = audio_->soundfonts();
                std::string instrument_label = "Synth (3-partial)";
                if (audio_->voice() == ScoreAudioController::Voice::Piano
                    && audio_->loaded_soundfont_index() >= 0)
                    instrument_label
                        = soundfonts[static_cast<size_t>(audio_->loaded_soundfont_index())]
                              .stem()
                              .string();
                if (ImGui::BeginCombo("instrument", instrument_label.c_str()))
                {
                    if (ImGui::Selectable("Synth (3-partial)",
                            audio_->voice() == ScoreAudioController::Voice::Synth))
                        audio_->use_synth();
                    for (int i = 0; i < static_cast<int>(soundfonts.size()); ++i)
                    {
                        const std::string name = soundfonts[static_cast<size_t>(i)].stem().string();
                        const bool active = audio_->voice() == ScoreAudioController::Voice::Piano
                            && audio_->loaded_soundfont_index() == i;
                        if (ImGui::Selectable(name.c_str(), active))
                            audio_->use_piano(i);
                    }
                    if (soundfonts.empty())
                        ImGui::TextDisabled("(no .sf2 in soundfonts/)");
                    ImGui::EndCombo();
                }
                bool audition = audio_->audition();
                if (ImGui::Checkbox("Audition (hear notes)", &audition))
                    audio_->set_audition(audition);
            }

            if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("accuracy EMA  %.0f%%", flow_.accuracy_ema() * 100.0);
                ImGui::Text("score %d   streak %d", flow_.score(), flow_.streak());
                ImGui::Text(
                    "misses %d   wrong notes %d", flow_.miss_count(), flow_.wrong_count());
                ImGui::Text("notes judged (lifetime) %d", session_->model().total_notes_judged());
                ImGui::Text("key %s (conf %.2f)   %zu chords, %zu figures",
                    key_name(session_->piece_profile().global_key.tonic_pc, session_->piece_profile().global_key.minor)
                        .c_str(),
                    session_->piece_profile().global_key.confidence, session_->piece_profile().chords.size(),
                    session_->piece_profile().figures.size());
                if (ImGui::Button("Clear progress + restart"))
                    clear_piece_progress();
                ImGui::SameLine();
                ImGui::TextDisabled("wipes this piece's record");
            }

            if (ImGui::CollapsingHeader("Timing drift", ImGuiTreeNodeFlags_DefaultOpen))
            {
                double sum = 0.0;
                int n = 0;
                for (const auto& [q, s] : session_->model().onset_stats())
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
                for (const auto& [q, s] : session_->model().onset_stats())
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
                    for (const auto& [bar, t] : session_->model().bar_tally())
                        if (t.miss > 0 || t.hit > 0)
                            bars.emplace_back(t.miss, bar);
                    std::sort(bars.rbegin(), bars.rend());
                    if (bars.empty())
                        ImGui::TextDisabled("(none yet)");
                    for (size_t i = 0; i < bars.size() && i < 16; ++i)
                    {
                        const int bar = bars[i].second;
                        const PlayerModel::BarTally& t = session_->model().bar_tally().at(bar);
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
                    for (const auto& [key, s] : session_->model().chord_stats())
                    {
                        const int trouble = s.miss + s.split - s.clean;
                        if (trouble > 0)
                            chords.emplace_back(trouble, key);
                    }
                    std::sort(chords.rbegin(), chords.rend());
                    if (chords.empty())
                        ImGui::TextDisabled("(none yet)");
                    for (size_t i = 0; i < chords.size() && i < 8; ++i)
                        ImGui::BulletText("%s   (%d)", chords[i].second.c_str(), chords[i].first);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Pitches (missed)"))
                {
                    std::vector<std::pair<int, int>> pitches; // miss, midi
                    for (const auto& [midi, s] : session_->model().pitch_stats())
                        if (s.miss > 0)
                            pitches.emplace_back(s.miss, midi);
                    std::sort(pitches.rbegin(), pitches.rend());
                    if (pitches.empty())
                        ImGui::TextDisabled("(none yet)");
                    for (size_t i = 0; i < pitches.size() && i < 8; ++i)
                        ImGui::BulletText("%s   (missed %d)", note_name(pitches[i].second).c_str(),
                            pitches[i].first);
                    ImGui::TreePop();
                }
            }

            if (composing_ && ImGui::CollapsingHeader("Composer program", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const int slot_now = stream_program_.slot_at(stream_position_q());
                const int planned = stream_program_.size();
                for (int s = slot_now; s < planned && s < slot_now + 10; ++s)
                {
                    const StreamBarPlan& p = stream_program_.plan(s);
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
                const int total = slicer_.bar_count();
                int encountered = 0;
                int mastered = 0;
                for (int b = 0; b < total; ++b)
                    if (session_->model().bar_encounters(b) > 0)
                    {
                        ++encountered;
                        if (session_->model().bar_mastery(b) >= 0.7)
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
}

} // namespace scoreview
} // namespace draxul
