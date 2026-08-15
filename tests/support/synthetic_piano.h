#pragma once

// Deterministic synthetic piano tones for listener tests (ScoreView ear
// plan E0, plans/scoreview-ear.md in the draxul-scoreview repository).
// Honest about being synthetic — real
// recordings supplement these — but it deliberately reproduces the acoustic
// facts the listener must survive: inharmonic partials (f_n =
// n*f0*sqrt(1+B*n^2)), exponential decay, a noisy attack transient, and
// controllable global/per-note detuning.

#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <vector>

namespace draxul::tests
{

struct SyntheticPianoConfig
{
    int sample_rate = 44100;
    double global_detune_cents = 0.0;
    std::map<int, double> per_note_detune_cents;
    double b_bass = 4e-4; // inharmonicity anchors, mirroring the listener
    double b_mid = 1e-4;
    double b_treble = 4e-3;
};

inline double synth_inharmonicity_b(const SyntheticPianoConfig& config, int midi)
{
    const double clamped = midi < 21 ? 21.0 : (midi > 108 ? 108.0 : static_cast<double>(midi));
    const auto lerp_log = [](double a, double b, double t) {
        return std::exp(std::log(a) + (std::log(b) - std::log(a)) * t);
    };
    if (clamped <= 60.0)
        return lerp_log(config.b_bass, config.b_mid, (clamped - 21.0) / 39.0);
    return lerp_log(config.b_mid, config.b_treble, (clamped - 60.0) / 48.0);
}

// Adds one note into `buffer` (mono float, config.sample_rate) starting at
// t_seconds. Deterministic: the attack noise uses xorshift seeded from the
// note parameters.
inline void synth_add_note(std::vector<float>& buffer, const SyntheticPianoConfig& config,
    double t_seconds, int midi, double duration_seconds, double amplitude)
{
    double detune = config.global_detune_cents;
    const auto found = config.per_note_detune_cents.find(midi);
    if (found != config.per_note_detune_cents.end())
        detune += found->second;
    const double f0 = 440.0 * std::pow(2.0, (midi - 69) / 12.0) * std::pow(2.0, detune / 1200.0);
    const double b_coeff = synth_inharmonicity_b(config, midi);
    const double nyquist = config.sample_rate * 0.5;

    const size_t start = static_cast<size_t>(t_seconds * config.sample_rate);
    const size_t length = static_cast<size_t>(duration_seconds * config.sample_rate);
    if (buffer.size() < start + length)
        buffer.resize(start + length, 0.0f);

    // Slower decay in the bass, faster in the treble.
    const double tau = std::max(0.15, 1.4 - (midi - 21) * 0.011);
    uint32_t rng = 0x9E3779B9u ^ (static_cast<uint32_t>(midi) * 2654435761u) ^ static_cast<uint32_t>(t_seconds * 1000.0);
    const auto next_random = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    for (size_t i = 0; i < length; ++i)
    {
        const double t = static_cast<double>(i) / config.sample_rate;
        const double attack = t < 0.002 ? t / 0.002 : 1.0;
        // Damped release over the final 30 ms — real pianos never click off.
        const double remaining = duration_seconds - t;
        const double release = remaining < 0.03 ? std::max(0.0, remaining / 0.03) : 1.0;
        const double envelope = attack * release * std::exp(-t / tau);
        double sample = 0.0;
        for (int n = 1; n <= 8; ++n)
        {
            const double fn = n * f0 * std::sqrt(1.0 + b_coeff * n * n);
            if (fn >= nyquist)
                break;
            const double partial_amp = std::pow(0.6, n - 1);
            // Deterministic per-partial phase offset (golden-angle spread).
            const double phase = 2.399963 * n * (midi % 12 + 1);
            sample += partial_amp
                * std::sin(2.0 * std::numbers::pi_v<double> * fn * t + phase);
        }
        // Percussive attack transient: a few ms of decaying noise.
        if (t < 0.008)
        {
            const double noise = (static_cast<double>(next_random()) / 4294967296.0 - 0.5) * 2.0;
            sample += noise * 0.35 * std::exp(-t / 0.003);
        }
        buffer[start + i] += static_cast<float>(amplitude * envelope * sample * 0.3);
    }
}

// Deterministic low-level noise floor across the whole buffer.
inline void synth_add_noise(std::vector<float>& buffer, double amplitude, uint32_t seed = 1234)
{
    uint32_t rng = seed == 0 ? 1u : seed;
    for (float& sample : buffer)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        sample += static_cast<float>(
            (static_cast<double>(rng) / 4294967296.0 - 0.5) * 2.0 * amplitude);
    }
}

} // namespace draxul::tests
