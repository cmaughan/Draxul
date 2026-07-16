#pragma once

// The runner's audio rig (kanban 21, ScoreAudioController): output stream,
// metronome ticks, audition voicing, instrument selection and mixing, MIDI
// play-thru, and soundfont staging. Owns every SDL-audio and synth detail so
// ScoreHost stays an orchestrator — the stream handle and synth voices never
// leak across this boundary. Internal to draxul-scoreview-host (no public
// header; the host holds it by unique_ptr behind a forward declaration).

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/metronome_synth.h>
#include <draxul/scoreview/midi_player_input.h>
#include <draxul/scoreview/soundfont_synth.h>

#include <cstdint>
#include <filesystem>
#include <vector>

struct SDL_AudioStream;

namespace draxul
{
namespace scoreview
{

class ScoreAudioController
{
public:
    enum class TickLevel : uint8_t
    {
        Off,
        Beats, // quarters, bar downbeat accented
        Eighths, // beats + quieter subdivision ticks
    };

    // Which instrument voices the audition and MIDI play-thru.
    enum class Voice : uint8_t
    {
        Synth, // the built-in three-partial tone
        Piano, // a staged .sf2 soundfont
    };

    ScoreAudioController() = default;
    ~ScoreAudioController();
    ScoreAudioController(const ScoreAudioController&) = delete;
    ScoreAudioController& operator=(const ScoreAudioController&) = delete;

    // Offers every .sf2 in `dir` (sorted). Loading stays lazy — the ~118 MB
    // parse happens on first selection, not startup.
    void stage_soundfonts(const std::filesystem::path& dir);
    const std::vector<std::filesystem::path>& soundfonts() const
    {
        return soundfont_paths_;
    }

    // The click track level. Setting it never opens the device (the pump
    // opens lazily); cycle_tick_level opens eagerly and reverts to Off when
    // the device is unavailable — exactly the old host behavior for the `t`
    // key vs the launch tokens.
    TickLevel tick_level() const
    {
        return tick_level_;
    }
    void set_tick_level(TickLevel level);
    void cycle_tick_level();

    // Audition: synthesized playback of notes as the playhead crosses them.
    bool audition() const
    {
        return audition_;
    }
    void set_audition(bool on);
    void toggle_audition();

    // Instrument voice for audition + MIDI play-thru.
    Voice voice() const
    {
        return voice_;
    }
    int loaded_soundfont_index() const
    {
        return piano_loaded_index_;
    }
    void use_synth();
    // Lazily loads soundfont `index`; false (with a WARN) when it can't
    // load, so callers stay on the synth voice.
    bool use_piano(int index);

    // Opens the shared output stream if needed (idempotent).
    bool ensure_output_stream();
    // Destroys the stream (idempotent; also runs at destruction).
    void shutdown();

    // Per-frame mixing pump: schedules ticks for grid crossings in
    // (p0, p1], audition notes for onsets crossed, then keeps ~70 ms
    // queued on the device.
    bool wants_pump() const
    {
        return tick_level_ != TickLevel::Off || audition_;
    }
    void pump(double p0_q, double p1_q, double dt, double quarters_per_bar,
        const FlowController& flow);

    // MIDI play-thru: most controllers are silent — voice the keys through
    // the selected instrument. Offs matter for the piano (the damper stops
    // the string); the synth ignores them.
    void voice_midi_events(const std::vector<MidiVoiceEvent>& events);

private:
    bool ensure_piano_voice();

    TickLevel tick_level_ = TickLevel::Eighths;
    bool audition_ = false;
    Voice voice_ = Voice::Synth;
    MetronomeSynth metronome_;
    ToneSynth tones_;
    std::vector<float> tone_buffer_;
    SoundFontSynth piano_;
    std::vector<float> piano_buffer_;
    std::vector<std::filesystem::path> soundfont_paths_; // staged .sf2 files
    int piano_selected_index_ = 0; // which staged font the picker chose
    int piano_loaded_index_ = -1; // which soundfont piano_ holds; -1 = none
    SDL_AudioStream* tick_stream_ = nullptr;
    std::vector<float> tick_buffer_;
};

} // namespace scoreview
} // namespace draxul
