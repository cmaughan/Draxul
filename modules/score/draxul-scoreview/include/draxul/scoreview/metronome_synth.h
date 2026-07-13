#pragma once

// The conveyor's click track (pure DSP, no audio device): short decaying
// sine ticks scheduled at absolute sample positions and rendered into
// caller-provided blocks. The host owns the output stream and the mapping
// from transport beats to sample times; this class just makes the sounds.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace draxul
{
namespace scoreview
{

struct MetronomeTuning
{
    int sample_rate = 44100;
    // Classic two-pitch click, subdivisions lower and quieter still.
    float accent_hz = 1661.2f; // bar downbeat
    float beat_hz = 1108.7f; // other beats
    float sub_hz = 830.6f; // subdivisions (eighths)
    float accent_gain = 0.55f;
    float beat_gain = 0.38f;
    float sub_gain = 0.18f;
    float attack_s = 0.001f; // sharp but click-free onset
    float decay_s = 0.025f; // exponential tail — crisp, not a drone
};

enum class TickKind : uint8_t
{
    Accent,
    Beat,
    Subdivision,
};

class MetronomeSynth
{
public:
    explicit MetronomeSynth(MetronomeTuning tuning = {});

    const MetronomeTuning& tuning() const
    {
        return tuning_;
    }

    // Absolute sample position of the next sample render() will produce.
    int64_t cursor() const
    {
        return cursor_;
    }

    // Schedules a tick at an absolute sample position. Positions before the
    // cursor are clamped to it (better late than dropped).
    void schedule_tick(int64_t at_sample, TickKind kind);

    // Renders `count` mono samples (overwrites `out`), advancing the cursor.
    void render(float* out, size_t count);

    void clear();

private:
    struct Voice
    {
        int64_t start_sample = 0;
        float hz = 0.0f;
        float gain = 0.0f;
    };

    MetronomeTuning tuning_;
    std::vector<Voice> voices_;
    int64_t cursor_ = 0;
};

} // namespace scoreview
} // namespace draxul
