#include <draxul/scoreview/soundfont_synth.h>

#include <draxul/log.h>

// TinySoundFont's implementation lives in exactly this translation unit.
#define TSF_IMPLEMENTATION
#include <tsf.h>

#include <algorithm>

namespace draxul
{
namespace scoreview
{

SoundFontSynth::~SoundFontSynth()
{
    if (tsf_ != nullptr)
        tsf_close(tsf_);
}

bool SoundFontSynth::load(const std::string& path, int sample_rate, std::string& error)
{
    tsf* loaded = tsf_load_filename(path.c_str());
    if (loaded == nullptr)
    {
        error = "could not load soundfont '" + path + "'";
        return false;
    }
    if (tsf_ != nullptr)
        tsf_close(tsf_);
    tsf_ = loaded;
    // Mono into the host's stream; a touch of headroom (the host mixes the
    // metronome on top). Max voices bounded so runaway sustain can't grow
    // the mix unbounded — TSF steals the oldest voice past the cap.
    tsf_set_output(tsf_, TSF_MONO, sample_rate, /*global_gain_db=*/-3.0f);
    tsf_set_max_voices(tsf_, 96);
    pending_.clear();
    DRAXUL_LOG_INFO(LogCategory::App, "score: soundfont loaded — %s (%d preset%s)",
        path.c_str(), tsf_get_presetcount(tsf_), tsf_get_presetcount(tsf_) == 1 ? "" : "s");
    return true;
}

void SoundFontSynth::schedule_note(int64_t at_sample, int midi, float velocity)
{
    if (tsf_ == nullptr || midi < 0 || midi > 127)
        return;
    Event event;
    event.at = std::max(at_sample, cursor_);
    event.midi = midi;
    event.velocity = std::clamp(velocity, 0.0f, 1.0f);
    event.on = true;
    pending_.push_back(event);
}

void SoundFontSynth::schedule_off(int64_t at_sample, int midi)
{
    if (tsf_ == nullptr || midi < 0 || midi > 127)
        return;
    Event event;
    event.at = std::max(at_sample, cursor_);
    event.midi = midi;
    pending_.push_back(event);
}

void SoundFontSynth::render(float* out, size_t count)
{
    if (tsf_ == nullptr)
    {
        std::fill(out, out + count, 0.0f);
        cursor_ += static_cast<int64_t>(count);
        return;
    }
    // Chunk the block at event boundaries so strikes and releases land on
    // their exact scheduled sample.
    std::stable_sort(pending_.begin(), pending_.end(),
        [](const Event& a, const Event& b) { return a.at < b.at; });
    size_t done = 0;
    size_t next = 0;
    while (done < count)
    {
        // Apply everything due at the current cursor.
        while (next < pending_.size() && pending_[next].at <= cursor_)
        {
            const Event& event = pending_[next];
            if (event.on)
                tsf_note_on(tsf_, /*preset=*/0, event.midi, event.velocity);
            else
                tsf_note_off(tsf_, /*preset=*/0, event.midi);
            ++next;
        }
        size_t chunk = count - done;
        if (next < pending_.size())
        {
            const int64_t until = pending_[next].at - cursor_;
            chunk = std::min(chunk, static_cast<size_t>(std::max<int64_t>(1, until)));
        }
        tsf_render_float(tsf_, out + done, static_cast<int>(chunk), /*mixing=*/0);
        done += chunk;
        cursor_ += static_cast<int64_t>(chunk);
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<long>(next));
}

void SoundFontSynth::clear()
{
    pending_.clear();
    if (tsf_ != nullptr)
        tsf_note_off_all(tsf_);
}

} // namespace scoreview
} // namespace draxul
