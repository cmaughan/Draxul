#include <draxul/scoreview/player_input_rig.h>

#include <draxul/log.h>
#include <draxul/scoreview/bot_player_input.h>
#include <draxul/scoreview/keyboard_player_input.h>
#include <draxul/scoreview/mic_player_input.h>

namespace draxul
{
namespace scoreview
{

PlayerInputRig::PlayerInputRig() = default;
PlayerInputRig::~PlayerInputRig() = default;

bool PlayerInputRig::select(const Selection& selection, FlowController& flow)
{
    clear();
    if (selection.kind == Kind::Bot)
    {
        input_ = std::make_unique<BotPlayerInput>(
            flow, selection.bot_pace_qpm, selection.bot_accuracy, selection.bot_seed);
        kind_ = Kind::Bot;
        return true;
    }
    if (selection.kind == Kind::Midi)
    {
        auto midi = std::make_unique<MidiPlayerInput>(selection.midi_port);
        if (midi->ok())
        {
            midi_ = midi.get();
            input_ = std::move(midi);
            kind_ = Kind::Midi;
            return true;
        }
        DRAXUL_LOG_WARN(LogCategory::App, "score: MIDI input failed (%s) — using dev keyboard",
            midi->error().c_str());
        // fall through to the keyboard
    }
    if (selection.kind == Kind::Mic)
    {
        // Opening is asynchronous (the TCC consent dialog can block for
        // minutes); Opening counts as engaged, and the host falls back to
        // the keyboard if mic_failed() ever reports the open failed.
        auto mic = std::make_unique<MicPlayerInput>(flow);
        if (mic->state() != MicPlayerInput::State::Failed)
        {
            mic_ = mic.get();
            input_ = std::move(mic);
            kind_ = Kind::Mic;
            return true;
        }
        DRAXUL_LOG_WARN(LogCategory::App, "score: %s — falling back to keyboard input",
            mic->error().c_str());
        // fall through to the keyboard
    }
    auto keyboard = std::make_unique<KeyboardPlayerInput>();
    keyboard_ = keyboard.get();
    input_ = std::move(keyboard);
    kind_ = Kind::Keyboard;
    return selection.kind == Kind::Keyboard;
}

void PlayerInputRig::clear()
{
    keyboard_ = nullptr;
    mic_ = nullptr;
    midi_ = nullptr;
    input_.reset();
    kind_ = Kind::None;
}

bool PlayerInputRig::feed_keyboard_note(int midi_pitch, double t_seconds)
{
    if (keyboard_ == nullptr)
        return false;
    keyboard_->push(midi_pitch, t_seconds);
    return true;
}

bool PlayerInputRig::mic_failed() const
{
    return mic_ != nullptr && mic_->state() == MicPlayerInput::State::Failed;
}

bool PlayerInputRig::mic_ready() const
{
    return mic_ != nullptr && mic_->state() == MicPlayerInput::State::Ready;
}

std::string PlayerInputRig::mic_error() const
{
    return mic_ != nullptr ? mic_->error() : std::string();
}

float PlayerInputRig::mic_level() const
{
    return mic_ != nullptr ? mic_->level() : 0.0f;
}

std::string PlayerInputRig::midi_port_name() const
{
    return midi_ != nullptr ? midi_->port_name() : std::string();
}

void PlayerInputRig::take_midi_voice_events(std::vector<MidiVoiceEvent>& out)
{
    if (midi_ != nullptr)
        midi_->take_voice_events(out);
}

std::vector<std::string> PlayerInputRig::list_midi_ports()
{
    return MidiPlayerInput::list_ports();
}

} // namespace scoreview
} // namespace draxul
