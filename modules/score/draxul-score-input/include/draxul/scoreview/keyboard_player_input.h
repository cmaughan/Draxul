#pragma once

#include <draxul/scoreview/player_input.h>

namespace draxul
{
namespace scoreview
{

// Dev-only scaffolding input (plans/scoreview-gate.md): the host translates
// computer-keyboard presses into MIDI pitches and pushes them here. Exists to
// build and feel the game loop before the milestone-3 microphone listener —
// deliberately minimal, not a product surface.
class KeyboardPlayerInput final : public IPlayerInput
{
public:
    void push(int midi_pitch, double t_seconds)
    {
        PlayerNoteEvent event;
        event.midi_pitch = midi_pitch;
        event.t_seconds = t_seconds;
        queue_.push_back(event);
    }

    void poll(double /*t_now_seconds*/, std::vector<PlayerNoteEvent>& out) override
    {
        out.insert(out.end(), queue_.begin(), queue_.end());
        queue_.clear();
    }

private:
    std::vector<PlayerNoteEvent> queue_;
};

} // namespace scoreview
} // namespace draxul
