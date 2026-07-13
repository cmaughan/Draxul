// The conveyor's click track (metronome_synth): tick placement, accent
// hierarchy, block-boundary continuity, and late-clamp behavior. The SDL
// output stream is host wiring, verified live.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/metronome_synth.h>

#include <cmath>
#include <vector>

using namespace draxul::scoreview;

namespace
{

float peak_in(const std::vector<float>& samples, size_t from, size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < to && i < samples.size(); ++i)
        peak = std::max(peak, std::abs(samples[i]));
    return peak;
}

} // namespace

TEST_CASE("metronome renders ticks at their scheduled samples", "[scoreview][metronome]")
{
    MetronomeSynth synth;
    synth.schedule_tick(1000, TickKind::Accent);
    synth.schedule_tick(5000, TickKind::Beat);

    // Render across two blocks to prove tails survive block boundaries.
    std::vector<float> samples(8000, 0.0f);
    synth.render(samples.data(), 3000);
    synth.render(samples.data() + 3000, 5000);
    CHECK(synth.cursor() == 8000);

    CHECK(peak_in(samples, 0, 990) == 0.0f); // silence before the first tick
    CHECK(peak_in(samples, 1000, 1500) > 0.1f); // accent sounds where scheduled
    CHECK(peak_in(samples, 4000, 4900) < 0.05f); // mostly decayed before the next
    CHECK(peak_in(samples, 5000, 5500) > 0.1f); // beat sounds where scheduled

    // The downbeat is louder than the beat, and both louder than the
    // subdivision (the audible hierarchy the ear navigates by).
    MetronomeSynth reference;
    reference.schedule_tick(0, TickKind::Subdivision);
    std::vector<float> sub(2000, 0.0f);
    reference.render(sub.data(), sub.size());
    const float accent_peak = peak_in(samples, 1000, 3000);
    const float beat_peak = peak_in(samples, 5000, 8000);
    const float sub_peak = peak_in(sub, 0, sub.size());
    CHECK(accent_peak > beat_peak);
    CHECK(beat_peak > sub_peak);
    CHECK(sub_peak > 0.05f);
}

TEST_CASE("metronome clamps late ticks instead of dropping them", "[scoreview][metronome]")
{
    MetronomeSynth synth;
    std::vector<float> first(4096, 0.0f);
    synth.render(first.data(), first.size());

    synth.schedule_tick(100, TickKind::Beat); // long past; must still sound
    std::vector<float> second(2000, 0.0f);
    synth.render(second.data(), second.size());
    CHECK(peak_in(second, 0, 400) > 0.1f);
}
