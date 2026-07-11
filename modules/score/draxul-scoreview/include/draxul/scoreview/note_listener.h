#pragma once

#include <draxul/scoreview/player_input.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

namespace draxul
{
namespace scoreview
{

// Every tunable of the acoustic listener, named and documented — the
// "recognition notebook" of plans/scoreview-ear.md made executable. Tests
// construct variants freely; future config can expose entries. Change
// defaults here, nowhere else.
struct ListenerTuning
{
    int sample_rate = 44100;
    int fft_size = 2048; // ~46 ms window, ~21.5 Hz bins at 44.1k
    int hop_size = 512; // ~11.6 ms analysis cadence

    // Onset detection: spectral flux NORMALIZED by total spectral energy
    // (scale-invariant — mic gain and note loudness cancel; a ringing tone's
    // heavy-tailed jitter stays small relative to its own energy while an
    // attack is large by construction). The normalized flux must be a strict
    // local peak above an adaptive (median-scaled) threshold, after warm-up,
    // with a refractory gap.
    int flux_median_frames = 24; // ~280 ms of flux history
    int onset_warmup_frames = 10; // no onsets until history exists
    double onset_threshold_ratio = 1.8; // flux must exceed ratio x median
    double onset_flux_floor = 5e-2; // normalized-flux floor
    double onset_energy_floor = 1e-4; // total-magnitude silence guard
    double onset_min_gap_ms = 55.0; // refractory: fastest repeat
    // Ringing notes BEAT (close partials of different notes swell in and
    // out), and each swell is genuine narrow-band spectral rise. A real
    // attack is broadband: require the rise to span many bins.
    int onset_min_rise_bins = 24;

    // Pitch confirmation: average partial magnitudes shortly after the
    // onset against just before it. Scoring the energy RISE makes sustained
    // previous notes and pedal wash invisible.
    int confirm_frames_after = 3; // ~35 ms at the default hop
    int confirm_frames_before = 2;

    // Inharmonic partial template: f_n = n * f0 * sqrt(1 + B * n^2), with B
    // log-interpolated through three anchors (stiff bass strings, mild
    // mid-keyboard, short stiff treble strings).
    int partial_count = 6;
    std::array<double, 6> partial_weights{ 1.0, 0.85, 0.65, 0.45, 0.3, 0.2 };
    double inharmonicity_bass = 4e-4; // ~A0 (midi 21)
    double inharmonicity_mid = 1e-4; // ~C4 (midi 60)
    double inharmonicity_treble = 4e-3; // ~C8 (midi 108)
    double tolerance_cents = 45.0; // peak search window per partial
    // Bass reality: at A1 one FFT bin spans hundreds of cents, so the
    // cents-tolerance must be floored at a fraction of a bin or low partials
    // can never match. Mid/treble keep the cents window (a full-bin floor
    // there would reopen the semitone-neighbor leak).
    double tolerance_min_bins = 0.6;

    // The attack transient is broadband and credits every pitch a little;
    // each partial's 3-bin rise is compensated by beta x the onset's mean
    // per-bin rise before scoring.
    double background_rise_beta = 1.5;

    // Acceptance (scores are normalized to the best swept candidate):
    // expected pitches accept at a friendlier bar; unexpected pitches must
    // clear a stricter one plus octave suppression to be reported as wrong
    // notes. min_total_rise ignores onsets with negligible spectral rise.
    double accept_relative = 0.30;
    double wrong_relative = 0.75;
    double min_total_rise = 1e-3;
    // Octave suppression: a candidate 12/19/24/28/31 semitones above an
    // accepted-or-plausible expected note needs this fraction of its score
    // from partials the lower note cannot supply.
    double independent_partial_fraction = 0.25;
    // Expected pitches scoring at least this fraction of best also anchor
    // suppression even when not accepted at this onset.
    double suppression_expected_floor = 0.15;

    // Expected pitch harmonically related to the onset's dominant note
    // (octave, twelfth, double octave...): their partial grids coincide, so
    // independence says nothing — but a REALLY played upper note adds its
    // full fundamental on top of the dominant note's weaker overtone there.
    // Predict that overtone from the dominant note's fundamental times the
    // amplitude model (amp of partial k ~ model^(k-1)); accept only when the
    // measured energy exceeds the prediction by the evidence ratio.
    double partial_amp_model = 0.6; // per-partial amplitude decay model
    double octave_evidence_ratio = 1.5;

    // Tuning calibration: cents deviation measured on accepted notes feeds
    // a global EMA plus a per-note profile (Railsback stretch and
    // per-string quirks). Deviations beyond the clamp are ignored as
    // measurement noise.
    double calibration_ema_global = 0.25;
    double calibration_ema_per_note = 0.3;
    double calibration_clamp_cents = 60.0;

    int sweep_low_midi = 21; // A0
    int sweep_high_midi = 108; // C8
};

// The acoustic listener (plans/scoreview-ear.md): mono float samples in,
// PlayerNoteEvent out. Pure and offline-testable — no audio device, no
// clock of its own (event times derive from the sample counter plus
// set_time_base). Verification-first: the expected pitch set focuses
// scoring, while a full-keyboard sweep still reports confident unexpected
// notes so wrong notes reach the judge.
class NoteListener
{
public:
    explicit NoteListener(ListenerTuning tuning = {});
    ~NoteListener();

    NoteListener(const NoteListener&) = delete;
    NoteListener& operator=(const NoteListener&) = delete;

    const ListenerTuning& tuning() const
    {
        return tuning_;
    }

    // The armed gate's still-pending pitches (may repeat for doubled notes).
    void set_expected_pitches(const std::vector<int>& midi_pitches);

    // Maps the sample clock to the caller's time domain: sample index 0 of
    // the *next* pushed sample corresponds to base_seconds.
    void set_time_base(double base_seconds);

    void push_samples(const float* samples, size_t count);
    std::vector<PlayerNoteEvent> take_events();

    // Calibration state (diagnostics + future persistence).
    double global_offset_cents() const
    {
        return global_offset_cents_;
    }
    double note_offset_cents(int midi_pitch) const;
    int onset_count() const
    {
        return onset_count_;
    }

private:
    struct PendingOnset
    {
        size_t frame_index = 0;
        double t_seconds = 0.0;
    };

    void process_frame();
    void evaluate_onset(const PendingOnset& onset);
    double effective_frequency(int midi_pitch) const;
    double inharmonicity_b(int midi_pitch) const;
    // Weighted partial energy rise for a pitch; per-partial rises appended
    // to `partial_rises` when non-null (octave suppression needs them).
    double score_pitch(int midi_pitch, const std::vector<float>& before,
        const std::vector<float>& after, double background_per_bin,
        std::vector<double>* partial_peak_hz, std::vector<double>* partial_rises) const;
    // Fraction of q's partial-rise energy at positions p cannot supply
    // (1.0 when q has no energy at all). The anti-phantom measure shared by
    // expected acceptance and wrong-note suppression.
    double independent_fraction(int pitch_q, int pitch_p, const std::vector<float>& before,
        const std::vector<float>& after, double background_per_bin) const;
    // For q whose grid coincides with anchor a's: does the lowest coinciding
    // partial of q carry more energy than a's amplitude model predicts for
    // its own overtone there? True = q is really sounding on top of a.
    bool excess_over_anchor(int pitch_q, int pitch_a, const std::vector<float>& before,
        const std::vector<float>& after, double background_per_bin) const;

    ListenerTuning tuning_;
    struct Fft;
    std::unique_ptr<Fft> fft_;

    std::vector<float> pending_; // samples not yet framed
    std::vector<float> window_; // Hann coefficients
    std::deque<std::vector<float>> magnitude_frames_; // recent spectra
    std::deque<double> flux_history_;
    std::deque<double> frame_times_; // hop-start time per flux entry
    std::deque<int> rise_bins_history_; // rise spread per flux entry
    std::vector<PendingOnset> pending_onsets_;
    std::vector<PlayerNoteEvent> events_;
    std::vector<int> expected_;

    size_t frame_count_ = 0;
    uint64_t consumed_samples_ = 0;
    double time_base_ = 0.0;
    double last_onset_seconds_ = -1.0;
    int onset_count_ = 0;

    double global_offset_cents_ = 0.0;
    std::unordered_map<int, double> per_note_offset_cents_;
};

} // namespace scoreview
} // namespace draxul
