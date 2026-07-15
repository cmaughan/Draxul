#pragma once

#include <vector>

namespace draxul
{
namespace scoreview
{

// One note the player produced, in the gate's judgment currency: a MIDI
// pitch and a wall-clock timestamp in seconds (steady-clock domain).
//
// This is the listener contract (plans/scoreview-gate.md): the milestone-3
// microphone front-end for acoustic pianos must reduce to exactly this
// stream. The dev keyboard and the verification bot are scaffolding
// implementations of the same seam.
struct PlayerNoteEvent
{
    int midi_pitch = -1;
    double t_seconds = 0.0;
    // Strike velocity 0..1 when the input source knows it (MIDI); 0 =
    // unknown. Judging ignores it — it only shades the play-thru voice.
    float velocity = 0.0f;
};

class IPlayerInput
{
public:
    virtual ~IPlayerInput() = default;

    // Appends any events produced since the last poll. t_now_seconds is the
    // caller's steady-clock now, for implementations that self-schedule.
    virtual void poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out) = 0;
};

} // namespace scoreview
} // namespace draxul
