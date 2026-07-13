#include <draxul/scoreview/metronome_synth.h>

#include <algorithm>
#include <cmath>
#include <numbers>

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
                voice.gain * envelope
                * std::sin(2.0 * std::numbers::pi_v<double> * voice.hz * t));
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

ToneSynth::ToneSynth(int sample_rate)
    : sample_rate_(sample_rate)
{
}

void ToneSynth::schedule_note(int64_t at_sample, int midi, float gain)
{
    Voice voice;
    voice.start_sample = std::max(at_sample, cursor_);
    voice.hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    voice.gain = gain;
    // Longer ring in the bass, shorter in the treble — piano-ish.
    voice.tau = std::clamp(0.9 - (midi - 21) * 0.006, 0.18, 0.9);
    voices_.push_back(voice);
}

void ToneSynth::render(float* out, size_t count)
{
    std::fill(out, out + count, 0.0f);
    const double rate = sample_rate_;
    constexpr double kAttack = 0.004;
    for (const Voice& voice : voices_)
    {
        const int64_t span = static_cast<int64_t>((kAttack + 6.0 * voice.tau) * rate);
        const int64_t begin = std::max(voice.start_sample, cursor_);
        const int64_t end = std::min(voice.start_sample + span, cursor_ + static_cast<int64_t>(count));
        for (int64_t at = begin; at < end; ++at)
        {
            const double t = static_cast<double>(at - voice.start_sample) / rate;
            const double envelope = (t < kAttack ? t / kAttack : std::exp(-(t - kAttack) / voice.tau));
            const double phase = 2.0 * std::numbers::pi_v<double> * voice.hz * t;
            // Three partials, 0.6^n amplitudes — enough to sound like a
            // note rather than a beep, cheap enough for full chords.
            const double sample = std::sin(phase) + 0.6 * std::sin(2.0 * phase)
                + 0.36 * std::sin(3.0 * phase);
            out[at - cursor_] += static_cast<float>(voice.gain * envelope * sample);
        }
    }
    cursor_ += static_cast<int64_t>(count);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                      [this, rate](const Voice& voice) {
                          const int64_t span = static_cast<int64_t>(
                              (0.004 + 6.0 * voice.tau) * rate);
                          return voice.start_sample + span <= cursor_;
                      }),
        voices_.end());
}

void ToneSynth::clear()
{
    voices_.clear();
}

} // namespace scoreview
} // namespace draxul
