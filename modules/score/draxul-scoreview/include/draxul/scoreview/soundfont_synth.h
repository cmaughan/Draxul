#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct tsf; // TinySoundFont stays private to the .cpp

namespace draxul
{
namespace scoreview
{

// An SF2-backed instrument voice (the "real piano") with the same
// sample-scheduled render contract as ToneSynth: events placed at absolute
// sample positions, mono float blocks, the host mixes into its one output
// stream. Backed by TinySoundFont; the bundled soundfont is FreePats' YDP
// Grand Piano (CC-BY 3.0), staged beside the app under soundfonts/.
class SoundFontSynth
{
public:
    SoundFontSynth() = default;
    ~SoundFontSynth();
    SoundFontSynth(const SoundFontSynth&) = delete;
    SoundFontSynth& operator=(const SoundFontSynth&) = delete;

    // Loads an .sf2 for mono rendering at sample_rate. Replaces any
    // previously loaded font. Returns false and fills `error` on failure.
    bool load(const std::string& path, int sample_rate, std::string& error);
    bool loaded() const
    {
        return tsf_ != nullptr;
    }

    // Absolute sample position of the next sample render() will produce.
    int64_t cursor() const
    {
        return cursor_;
    }

    // Schedules a strike (velocity 0..1) / release at an absolute sample
    // position. Positions before the cursor clamp to it.
    void schedule_note(int64_t at_sample, int midi, float velocity);
    void schedule_off(int64_t at_sample, int midi);

    // Renders `count` mono samples (overwrites `out`), advancing the cursor
    // and applying scheduled events at their exact sample boundaries.
    void render(float* out, size_t count);

    // Releases every sounding voice and drops pending events.
    void clear();

private:
    struct Event
    {
        int64_t at = 0;
        int midi = -1;
        float velocity = 0.0f;
        bool on = false;
    };

    tsf* tsf_ = nullptr;
    std::vector<Event> pending_;
    int64_t cursor_ = 0;
};

} // namespace scoreview
} // namespace draxul
