#pragma once

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/player_input.h>

#include <cstddef>
#include <cstdint>

namespace draxul
{
namespace scoreview
{

// A deterministic scripted player: plays each armed gate at its own pace
// (independent of the transport tempo, so adaptation can be driven both up
// and down) with a configurable accuracy. The gate milestone's verification
// instrument, and — deliberately — the future regression harness for the
// microphone listener: both fill the same IPlayerInput seam
// (plans/scoreview-gate.md).
class BotPlayerInput final : public IPlayerInput
{
public:
    BotPlayerInput(const FlowController& flow, double pace_qpm, double accuracy, uint32_t seed);

    void poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out) override;

private:
    uint32_t next_random();

    const FlowController& flow_;
    double pace_qpm_;
    double accuracy_;
    uint32_t rng_state_;
    double next_play_t_ = -1.0;
    double last_play_q_ = -1.0;
    size_t scheduled_gate_ = static_cast<size_t>(-1);
};

} // namespace scoreview
} // namespace draxul
