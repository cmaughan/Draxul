#include <draxul/scoreview/bot_player_input.h>

#include <algorithm>

namespace draxul
{
namespace scoreview
{

BotPlayerInput::BotPlayerInput(
    const FlowController& flow, double pace_qpm, double accuracy, uint32_t seed)
    : flow_(flow)
    , pace_qpm_(std::max(pace_qpm, 1.0))
    , accuracy_(std::clamp(accuracy, 0.0, 1.0))
    , rng_state_(seed == 0 ? 0x9E3779B9u : seed)
{
}

uint32_t BotPlayerInput::next_random()
{
    // xorshift32 — deterministic across platforms, no <random> state size.
    uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return x;
}

void BotPlayerInput::poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out)
{
    if (!flow_.playing() || flow_.mode() != FlowController::TransportMode::Gate || !flow_.gates_ready())
        return;
    const size_t armed = flow_.armed_gate_index();
    if (armed >= flow_.gates().size())
        return;
    const FlowController::Gate& gate = flow_.gates()[armed];
    if (gate.required == 0)
        return; // auto gate; the transport opens it

    // Schedule this gate relative to the previous play at the bot's own pace.
    if (scheduled_gate_ != armed)
    {
        scheduled_gate_ = armed;
        const double gate_q = flow_.onsets()[armed].qstamp;
        const double gap_q = last_play_q_ < 0.0 ? 0.0 : std::max(0.0, gate_q - last_play_q_);
        const double base_t = next_play_t_ < 0.0 ? t_now_seconds + 0.05 : next_play_t_;
        next_play_t_ = base_t + gap_q * 60.0 / pace_qpm_;
        last_play_q_ = gate_q;
    }
    if (t_now_seconds < next_play_t_)
        return;

    // Play the whole gate: each required pitch, degraded by accuracy.
    for (const int pitch : flow_.armed_required_pitches())
    {
        PlayerNoteEvent event;
        const bool correct = (next_random() % 10000) < accuracy_ * 10000.0;
        event.midi_pitch = correct ? pitch : pitch + 1;
        event.t_seconds = t_now_seconds;
        out.push_back(event);
    }
}

} // namespace scoreview
} // namespace draxul
