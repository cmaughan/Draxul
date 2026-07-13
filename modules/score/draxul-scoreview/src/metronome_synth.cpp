#include <draxul/scoreview/metronome_synth.h>

#include <algorithm>
#include <cmath>

namespace draxul
{
namespace scoreview
{

MetronomeSynth::MetronomeSynth(MetronomeTuning tuning)
    : tuning_(tuning)
{
}

void MetronomeSynth::schedule_tick(int64_t at_sample, TickKind kind)
{
    Voice voice;
    voice.start_sample = std::max(at_sample, cursor_);
    switch (kind)
    {
    case TickKind::Accent:
        voice.hz = tuning_.accent_hz;
        voice.gain = tuning_.accent_gain;
        break;
    case TickKind::Beat:
        voice.hz = tuning_.beat_hz;
        voice.gain = tuning_.beat_gain;
        break;
    case TickKind::Subdivision:
        voice.hz = tuning_.sub_hz;
        voice.gain = tuning_.sub_gain;
        break;
    }
    voices_.push_back(voice);
}

void MetronomeSynth::render(float* out, size_t count)
{
    const double rate = tuning_.sample_rate;
    const double tau = std::max(1e-4f, tuning_.decay_s);
    const double attack = std::max(1e-5f, tuning_.attack_s);
    // A voice is spent once its envelope has decayed to inaudibility.
    const int64_t voice_span = static_cast<int64_t>((attack + 8.0 * tau) * rate);

    std::fill(out, out + count, 0.0f);
    for (const Voice& voice : voices_)
    {
        const int64_t begin = std::max(voice.start_sample, cursor_);
        const int64_t end = std::min(voice.start_sample + voice_span,
            cursor_ + static_cast<int64_t>(count));
        for (int64_t at = begin; at < end; ++at)
        {
            const double t = static_cast<double>(at - voice.start_sample) / rate;
            const double envelope = (t < attack ? t / attack : std::exp(-(t - attack) / tau));
            out[at - cursor_] += static_cast<float>(
                voice.gain * envelope * std::sin(2.0 * M_PI * voice.hz * t));
        }
    }
    cursor_ += static_cast<int64_t>(count);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                      [this, voice_span](const Voice& voice) {
                          return voice.start_sample + voice_span <= cursor_;
                      }),
        voices_.end());
}

void MetronomeSynth::clear()
{
    voices_.clear();
}

} // namespace scoreview
} // namespace draxul
