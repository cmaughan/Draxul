#pragma once

// Owns the live player-input implementation and the selection/fallback
// policy (plans/scoreview-gate.md): which device judges the session is a
// swap-in-place decision — verdicts, score, and transport are untouched by a
// swap. The host talks to the seam (IPlayerInput::poll) plus the narrow
// per-device capabilities below; the concrete input types stay private to
// the rig, so the host never names a device class again.

#include <draxul/scoreview/midi_player_input.h> // MidiVoiceEvent (play-thru currency)
#include <draxul/scoreview/player_input.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

class FlowController;
class KeyboardPlayerInput;
class MicPlayerInput;

class PlayerInputRig
{
public:
    enum class Kind : uint8_t
    {
        None, // cleared (no gate session)
        Keyboard, // dev piano row (scaffolding)
        Bot, // deterministic verification player
        Mic, // the acoustic listener (the eventual product input)
        Midi, // a hardware MIDI keyboard — lossless ground truth
    };

    struct Selection
    {
        Kind kind = Kind::Keyboard;
        double bot_pace_qpm = 0.0;
        double bot_accuracy = 1.0;
        uint32_t bot_seed = 20260711u;
        int midi_port = -1;
    };

    PlayerInputRig();
    ~PlayerInputRig();
    PlayerInputRig(const PlayerInputRig&) = delete;
    PlayerInputRig& operator=(const PlayerInputRig&) = delete;

    // Swaps the live input. MIDI that can't open falls back to the dev
    // keyboard immediately (WARN logged); a mic still Opening counts as
    // engaged — the TCC consent dialog can block for minutes — and the host
    // polls mic_failed() each pump for the late fallback. Returns whether
    // the REQUESTED input engaged.
    bool select(const Selection& selection, FlowController& flow);
    void clear();

    Kind kind() const
    {
        return kind_;
    }
    bool active() const
    {
        return input_ != nullptr;
    }
    // The judging seam. Null only when cleared.
    IPlayerInput* input()
    {
        return input_.get();
    }

    // Narrow device capabilities — safe no-ops when that device isn't live.
    // Dev piano row: pushes a note into the keyboard input (false otherwise).
    bool feed_keyboard_note(int midi_pitch, double t_seconds);
    // Microphone: async open state (the pump's fallback poll), level meter.
    bool mic_failed() const;
    bool mic_ready() const;
    std::string mic_error() const;
    float mic_level() const;
    // MIDI: the live port's name (empty when MIDI isn't live) and the voice
    // stream for the play-thru instrument (drain AFTER polling the seam).
    std::string midi_port_name() const;
    void take_midi_voice_events(std::vector<MidiVoiceEvent>& out);
    // Port enumeration; only call while a picker is open — each probe
    // touches the CoreMIDI client (see the inspector's combo note).
    static std::vector<std::string> list_midi_ports();

private:
    std::unique_ptr<IPlayerInput> input_;
    Kind kind_ = Kind::None;
    // Borrowed device views of input_ (exactly one non-null while live);
    // the aliasing is the rig's private business.
    KeyboardPlayerInput* keyboard_ = nullptr;
    MicPlayerInput* mic_ = nullptr;
    MidiPlayerInput* midi_ = nullptr;
};

} // namespace scoreview
} // namespace draxul
