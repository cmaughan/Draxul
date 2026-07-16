#include <draxul/scoreview/note_listener.h>

#include <kiss_fftr.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace draxul
{
namespace scoreview
{

namespace
{

constexpr double kA4Hz = 440.0;
constexpr int kA4Midi = 69;

double midi_to_hz(int midi)
{
    return kA4Hz * std::pow(2.0, (midi - kA4Midi) / 12.0);
}

double cents_to_ratio(double cents)
{
    return std::pow(2.0, cents / 1200.0);
}

double median_of(std::deque<double> values)
{
    if (values.empty())
        return 0.0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<long>(mid), values.end());
    return values[mid];
}

// Octave-family intervals (semitones) whose upper note shares most partials
// with the lower: octave, octave+fifth, two octaves, two octaves+third(+),
// two octaves+fifth.
constexpr int kSharedPartialIntervals[] = { 12, 19, 24, 28, 31 };

} // namespace

struct NoteListener::Fft
{
    explicit Fft(int n)
        : in(static_cast<size_t>(n), 0.0f)
        , out(static_cast<size_t>(n) / 2 + 1)
    {
        cfg = kiss_fftr_alloc(n, 0, nullptr, nullptr);
    }
    ~Fft()
    {
        kiss_fftr_free(cfg);
    }
    kiss_fftr_cfg cfg = nullptr;
    std::vector<float> in;
    std::vector<kiss_fft_cpx> out;
};

NoteListener::NoteListener(ListenerTuning tuning)
    : tuning_(tuning)
    , fft_(std::make_unique<Fft>(tuning.fft_size))
{
    window_.resize(static_cast<size_t>(tuning_.fft_size));
    for (int i = 0; i < tuning_.fft_size; ++i)
    {
        window_[static_cast<size_t>(i)] = static_cast<float>(
            0.5 * (1.0 - std::cos(2.0 * std::numbers::pi_v<double> * i / (tuning_.fft_size - 1))));
    }
    pending_.reserve(static_cast<size_t>(tuning_.fft_size) * 2);
}

NoteListener::~NoteListener() = default;

void NoteListener::set_expected_pitches(const std::vector<int>& midi_pitches)
{
    expected_ = midi_pitches;
}

void NoteListener::set_time_base(double base_seconds)
{
    time_base_ = base_seconds - static_cast<double>(consumed_samples_) / tuning_.sample_rate;
}

double NoteListener::note_offset_cents(int midi_pitch) const
{
    const auto found = per_note_offset_cents_.find(midi_pitch);
    return global_offset_cents_ + (found == per_note_offset_cents_.end() ? 0.0 : found->second);
}

double NoteListener::effective_frequency(int midi_pitch) const
{
    return midi_to_hz(midi_pitch) * cents_to_ratio(note_offset_cents(midi_pitch));
}

double NoteListener::inharmonicity_b(int midi_pitch) const
{
    // Log-domain interpolation through the bass / mid / treble anchors.
    const double clamped = std::clamp(midi_pitch, 21, 108);
    const auto lerp_log = [](double a, double b, double t) {
        return std::exp(std::log(a) + (std::log(b) - std::log(a)) * t);
    };
    if (clamped <= 60.0)
        return lerp_log(tuning_.inharmonicity_bass, tuning_.inharmonicity_mid,
            (clamped - 21.0) / 39.0);
    return lerp_log(
        tuning_.inharmonicity_mid, tuning_.inharmonicity_treble, (clamped - 60.0) / 48.0);
}

void NoteListener::push_samples(const float* samples, size_t count)
{
    const size_t fft_size = static_cast<size_t>(tuning_.fft_size);
    const size_t hop = static_cast<size_t>(tuning_.hop_size);
    for (size_t i = 0; i < count; ++i)
    {
        pending_.push_back(samples[i]);
        ++consumed_samples_;
        if (pending_.size() >= fft_size && (pending_.size() - fft_size) % hop == 0)
        {
            process_frame();
        }
    }
    // Bound the pending buffer: keep the most recent fft_size-1 samples plus
    // whatever remains toward the next hop boundary.
    if (pending_.size() > fft_size + hop * 4)
    {
        const size_t keep = fft_size - hop;
        const size_t excess = pending_.size() - keep;
        const size_t drop = (excess / hop) * hop;
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<long>(drop));
    }
}

void NoteListener::process_frame()
{
    const size_t fft_size = static_cast<size_t>(tuning_.fft_size);
    const size_t nbins = fft_size / 2 + 1;
    const size_t start = pending_.size() - fft_size;
    for (size_t i = 0; i < fft_size; ++i)
        fft_->in[i] = pending_[start + i] * window_[i];
    kiss_fftr(fft_->cfg, fft_->in.data(), fft_->out.data());

    std::vector<float> mags(nbins);
    const float norm = 2.0f / static_cast<float>(fft_size);
    for (size_t b = 0; b < nbins; ++b)
    {
        const kiss_fft_cpx& c = fft_->out[b];
        mags[b] = std::sqrt(c.r * c.r + c.i * c.i) * norm;
    }

    // Half-wave-rectified spectral flux, normalized by total spectral
    // energy: scale-invariant (mic gain cancels) and a ringing tone's
    // jitter stays small relative to its own energy.
    double flux = 0.0;
    int rise_bins = 0;
    double total_now = 0.0;
    for (size_t b = 0; b < nbins; ++b)
        total_now += mags[b];
    if (!magnitude_frames_.empty())
    {
        const std::vector<float>& prev = magnitude_frames_.back();
        double rectified = 0.0;
        double total_prev = 0.0;
        for (size_t b = 0; b < nbins; ++b)
            total_prev += prev[b];
        const double bin_epsilon = 0.5 * std::max(total_now, total_prev) / static_cast<double>(nbins);
        for (size_t b = 0; b < nbins; ++b)
        {
            const double rise = std::max(0.0f, mags[b] - prev[b]);
            rectified += rise;
            rise_bins += rise > bin_epsilon ? 1 : 0;
        }
        const double denom = 0.5 * (total_now + total_prev);
        flux = denom > tuning_.onset_energy_floor ? rectified / denom : 0.0;
    }

    magnitude_frames_.push_back(std::move(mags));
    const size_t keep_frames = static_cast<size_t>(
        tuning_.confirm_frames_before + tuning_.confirm_frames_after + 6);
    while (magnitude_frames_.size() > keep_frames)
        magnitude_frames_.pop_front();

    // The attack lives somewhere in this frame's newest hop.
    const double t_frame = time_base_ + static_cast<double>(consumed_samples_ - static_cast<uint64_t>(tuning_.hop_size)) / tuning_.sample_rate;
    flux_history_.push_back(flux);
    frame_times_.push_back(t_frame);
    rise_bins_history_.push_back(rise_bins);
    while (flux_history_.size() > static_cast<size_t>(tuning_.flux_median_frames))
    {
        flux_history_.pop_front();
        frame_times_.pop_front();
        rise_bins_history_.pop_front();
    }

    // Onset decision for the PREVIOUS frame: its flux must be a strict local
    // peak (kills decay-phase jitter), above the adaptive threshold, after
    // warm-up, and past the refractory gap.
    const size_t n = flux_history_.size();
    if (n >= 3 && frame_count_ + 1 >= static_cast<size_t>(tuning_.onset_warmup_frames))
    {
        const double f_prev2 = flux_history_[n - 3];
        const double f_prev = flux_history_[n - 2];
        const double f_now = flux_history_[n - 1];
        std::deque<double> baseline(flux_history_.begin(), flux_history_.end() - 2);
        const double threshold = std::max(
            tuning_.onset_flux_floor, median_of(baseline) * tuning_.onset_threshold_ratio);
        const double t_onset = frame_times_[n - 2];
        const int spread = rise_bins_history_[n - 2];
        if (f_prev > f_prev2 && f_prev >= f_now && f_prev > threshold && spread >= tuning_.onset_min_rise_bins && (last_onset_seconds_ < 0.0 || (t_onset - last_onset_seconds_) * 1000.0 >= tuning_.onset_min_gap_ms))
        {
            pending_onsets_.push_back({ frame_count_ - 1, t_onset });
            last_onset_seconds_ = t_onset;
            ++onset_count_;
        }
    }
    ++frame_count_;

    // Confirm onsets whose post-window is now complete.
    for (auto it = pending_onsets_.begin(); it != pending_onsets_.end();)
    {
        if (frame_count_ >= it->frame_index + static_cast<size_t>(tuning_.confirm_frames_after))
        {
            evaluate_onset(*it);
            it = pending_onsets_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

double NoteListener::score_pitch(int midi_pitch, const std::vector<float>& before,
    const std::vector<float>& after, double background_per_bin,
    std::vector<double>* partial_peak_hz, std::vector<double>* partial_rises) const
{
    const double f0 = effective_frequency(midi_pitch);
    const double b_coeff = inharmonicity_b(midi_pitch);
    const double tol = cents_to_ratio(tuning_.tolerance_cents);
    const double bin_hz = static_cast<double>(tuning_.sample_rate) / tuning_.fft_size;
    const double nyquist = tuning_.sample_rate * 0.49;
    const int nbins = static_cast<int>(after.size());

    double raw = 0.0;
    for (int n = 1; n <= tuning_.partial_count; ++n)
    {
        const double fn = n * f0 * std::sqrt(1.0 + b_coeff * n * n);
        if (fn >= nyquist)
            break;
        // Cents tolerance floored in bins: bass partials live in bins that
        // span hundreds of cents, so a pure cents window can't even see them.
        const double half_hz = std::max(fn * (tol - 1.0), tuning_.tolerance_min_bins * bin_hz);
        int lo = static_cast<int>(std::floor((fn - half_hz) / bin_hz));
        int hi = static_cast<int>(std::ceil((fn + half_hz) / bin_hz));
        lo = std::clamp(lo, 2, nbins - 2); // bin 1 is near-DC rumble
        hi = std::clamp(hi, 2, nbins - 2);
        if (hi < lo)
            continue;

        int peak = lo;
        for (int b = lo + 1; b <= hi; ++b)
        {
            if (after[static_cast<size_t>(b)] > after[static_cast<size_t>(peak)])
                peak = b;
        }
        // A peak on the window edge with the slope still rising outward is
        // not our partial — it's the skirt of a stronger neighbor outside
        // the window (a semitone/whole-tone note's mainlobe reaching in).
        const bool skirt = (peak == lo && after[static_cast<size_t>(peak - 1)] > after[static_cast<size_t>(peak)]) || (peak == hi && after[static_cast<size_t>(peak + 1)] > after[static_cast<size_t>(peak)]);
        double rise = 0.0;
        for (int b = peak - 1; b <= peak + 1; ++b)
        {
            rise += std::max(
                0.0f, after[static_cast<size_t>(b)] - before[static_cast<size_t>(b)]);
        }
        if (skirt)
            rise = 0.0;
        // Broadband attack noise credits every window a little; subtract the
        // expected background for these 3 bins before believing the partial.
        rise = std::max(0.0, rise - 3.0 * background_per_bin * tuning_.background_rise_beta);

        // Sub-bin truth: low partials sit in bins ~80 cents wide, so a
        // semitone neighbor's window can grab this note's peak *bin*. The
        // parabolic-interpolated peak frequency is sub-bin accurate — weight
        // the rise by how close the measured peak is to THIS pitch's
        // expected partial (quadratic falloff, zero beyond tolerance).
        const double m0 = after[static_cast<size_t>(peak - 1)];
        const double m1 = after[static_cast<size_t>(peak)];
        const double m2 = after[static_cast<size_t>(peak + 1)];
        const double denom = m0 - 2.0 * m1 + m2;
        const double shift = std::abs(denom) > 1e-12 ? std::clamp(0.5 * (m0 - m2) / denom, -0.5, 0.5) : 0.0;
        const double peak_hz = (peak + shift) * bin_hz;
        const double dev_hz = peak_hz - fn;
        const double kernel = std::max(0.0, 1.0 - (dev_hz / half_hz) * (dev_hz / half_hz));
        rise *= kernel;

        const double weighted = tuning_.partial_weights[static_cast<size_t>(n - 1)] * rise;
        raw += weighted;
        if (partial_rises != nullptr)
            partial_rises->push_back(weighted);
        if (partial_peak_hz != nullptr)
            partial_peak_hz->push_back(peak_hz);
    }
    return raw;
}

double NoteListener::independent_fraction(int pitch_q, int pitch_p,
    const std::vector<float>& before, const std::vector<float>& after,
    double background_per_bin) const
{
    const double bin_hz = static_cast<double>(tuning_.sample_rate) / tuning_.fft_size;
    const double tol = cents_to_ratio(tuning_.tolerance_cents);
    const double fq = effective_frequency(pitch_q);
    const double fp = effective_frequency(pitch_p);
    const double bq = inharmonicity_b(pitch_q);
    const double bp = inharmonicity_b(pitch_p);
    std::vector<double> q_rises;
    score_pitch(pitch_q, before, after, background_per_bin, nullptr, &q_rises);
    double independent = 0.0;
    double total = 0.0;
    for (size_t n = 0; n < q_rises.size(); ++n)
    {
        const int qi = static_cast<int>(n) + 1;
        const double fqn = qi * fq * std::sqrt(1.0 + bq * qi * qi);
        const double half_hz = std::max(fqn * (tol - 1.0), tuning_.tolerance_min_bins * bin_hz);
        bool coincides = false;
        for (int pi = 1; pi <= tuning_.partial_count * 2; ++pi)
        {
            const double fpn = pi * fp * std::sqrt(1.0 + bp * pi * pi);
            if (std::abs(fqn - fpn) <= half_hz)
            {
                coincides = true;
                break;
            }
        }
        total += q_rises[n];
        if (!coincides)
            independent += q_rises[n];
    }
    return total > 0.0 ? independent / total : 1.0;
}

bool NoteListener::excess_over_anchor(int pitch_q, int pitch_a,
    const std::vector<float>& before, const std::vector<float>& after,
    double background_per_bin) const
{
    const double bin_hz = static_cast<double>(tuning_.sample_rate) / tuning_.fft_size;
    const double tol = cents_to_ratio(tuning_.tolerance_cents);
    const double fq = effective_frequency(pitch_q);
    const double fa = effective_frequency(pitch_a);
    const double bq = inharmonicity_b(pitch_q);
    const double ba = inharmonicity_b(pitch_a);
    std::vector<double> q_rises;
    std::vector<double> a_rises;
    score_pitch(pitch_q, before, after, background_per_bin, nullptr, &q_rises);
    score_pitch(pitch_a, before, after, background_per_bin, nullptr, &a_rises);
    if (q_rises.empty() || a_rises.empty())
        return false;

    // Robust anchor strength (fundamental-scale): each uncontested anchor
    // partial estimates it through the amplitude model, and the median
    // survives both a suppressed fundamental (a ringing neighbor in the
    // before-frames eats its rise) and an inflated partial (another
    // simultaneous note landing on it). Windows q's own grid touches are
    // contested — a really-played q would inflate them.
    std::deque<double> estimates;
    for (size_t m = 0; m < a_rises.size(); ++m)
    {
        const int k = static_cast<int>(m) + 1;
        const double fak = k * fa * std::sqrt(1.0 + ba * k * k);
        const double half_a = std::max(fak * (tol - 1.0), tuning_.tolerance_min_bins * bin_hz);
        bool contested = false;
        for (int qi = 1; qi <= tuning_.partial_count * 2 && !contested; ++qi)
        {
            const double fqn = qi * fq * std::sqrt(1.0 + bq * qi * qi);
            contested = std::abs(fqn - fak) <= half_a;
        }
        if (contested)
            continue;
        estimates.push_back(a_rises[m] / tuning_.partial_weights[m] / std::pow(tuning_.partial_amp_model, k - 1));
    }
    const double a_strength = estimates.empty()
        ? a_rises[0] / tuning_.partial_weights[0]
        : median_of(estimates);

    for (size_t n = 0; n < q_rises.size(); ++n)
    {
        const int qi = static_cast<int>(n) + 1;
        const double fqn = qi * fq * std::sqrt(1.0 + bq * qi * qi);
        const double half_hz = std::max(fqn * (tol - 1.0), tuning_.tolerance_min_bins * bin_hz);
        for (int k = 1; k <= tuning_.partial_count * 2; ++k)
        {
            const double fak = k * fa * std::sqrt(1.0 + ba * k * k);
            if (std::abs(fqn - fak) > half_hz)
                continue;
            // Lowest coinciding pair decides: the anchor's k-th partial
            // should carry ~model^(k-1) of its strength; a really played
            // q adds its own partial's full energy on top.
            const double predicted = a_strength * std::pow(tuning_.partial_amp_model, k - 1);
            const double measured = q_rises[n] / tuning_.partial_weights[n];
            return measured > tuning_.octave_evidence_ratio * predicted;
        }
    }
    return false;
}

void NoteListener::evaluate_onset(const PendingOnset& onset)
{
    const size_t nbins = static_cast<size_t>(tuning_.fft_size) / 2 + 1;
    // Deque index of the onset frame relative to the retained window.
    const size_t oldest_abs = frame_count_ - magnitude_frames_.size();
    if (onset.frame_index < oldest_abs)
        return; // retention window overrun (shouldn't happen at sane tunings)
    const size_t onset_idx = onset.frame_index - oldest_abs;

    std::vector<float> before(nbins, 0.0f);
    std::vector<float> after(nbins, 0.0f);
    int before_n = 0;
    for (int k = 1; k <= tuning_.confirm_frames_before; ++k)
    {
        if (onset_idx < static_cast<size_t>(k))
            break;
        const std::vector<float>& frame = magnitude_frames_[onset_idx - static_cast<size_t>(k)];
        for (size_t b = 0; b < nbins; ++b)
            before[b] += frame[b];
        ++before_n;
    }
    int after_n = 0;
    for (int k = 0; k < tuning_.confirm_frames_after; ++k)
    {
        const size_t idx = onset_idx + static_cast<size_t>(k);
        if (idx >= magnitude_frames_.size())
            break;
        const std::vector<float>& frame = magnitude_frames_[idx];
        for (size_t b = 0; b < nbins; ++b)
            after[b] += frame[b];
        ++after_n;
    }
    if (after_n == 0)
        return;
    for (size_t b = 0; b < nbins; ++b)
    {
        after[b] /= static_cast<float>(after_n);
        if (before_n > 0)
            before[b] /= static_cast<float>(before_n);
    }

    double total_rise = 0.0;
    for (size_t b = 0; b < nbins; ++b)
        total_rise += std::max(0.0f, after[b] - before[b]);
    if (total_rise < tuning_.min_total_rise)
        return;
    const double background_per_bin = total_rise / static_cast<double>(nbins);

    // Score the whole keyboard once.
    const int low = tuning_.sweep_low_midi;
    const int high = tuning_.sweep_high_midi;
    std::vector<double> scores(static_cast<size_t>(high - low + 1), 0.0);
    double best = 0.0;
    int best_pitch = low;
    for (int p = low; p <= high; ++p)
    {
        scores[static_cast<size_t>(p - low)] = score_pitch(p, before, after, background_per_bin, nullptr, nullptr);
        if (scores[static_cast<size_t>(p - low)] > best)
        {
            best = scores[static_cast<size_t>(p - low)];
            best_pitch = p;
        }
    }
    if (best <= 0.0)
        return;

    // Accept expected pitches (distinct — a piano key cannot be doubled),
    // ascending so lower octaves are settled before their uppers.
    std::vector<int> accepted;
    std::vector<int> distinct_expected = expected_;
    std::sort(distinct_expected.begin(), distinct_expected.end());
    distinct_expected.erase(
        std::unique(distinct_expected.begin(), distinct_expected.end()), distinct_expected.end());
    for (const int p : distinct_expected)
    {
        if (p < low || p > high)
            continue;
        const double score = scores[static_cast<size_t>(p - low)];
        if (score >= tuning_.accept_relative * best)
        {
            // Anti-phantom gauntlet: against every anchor (already-accepted
            // notes plus the sweep's dominant pitch), p must either show
            // partial energy the anchor cannot supply (independence — C4
            // "heard" under a played C5 has none), or, when the grids fully
            // coincide (octave, twelfth, double octave: real chords!),
            // exceed the anchor's predicted overtone amplitude at the
            // lowest shared partial (excess evidence — a played G4 over C3
            // adds its whole fundamental to C3's modest 3rd partial).
            std::vector<int> anchors = accepted;
            if (p != best_pitch && std::find(anchors.begin(), anchors.end(), best_pitch) == anchors.end())
                anchors.push_back(best_pitch);
            bool phantom = false;
            for (const int anchor : anchors)
            {
                if (anchor == p)
                    continue;
                if (independent_fraction(p, anchor, before, after, background_per_bin) >= tuning_.independent_partial_fraction)
                    continue;
                if (excess_over_anchor(p, anchor, before, after, background_per_bin))
                    continue;
                phantom = true;
                break;
            }
            if (phantom)
                continue;
            accepted.push_back(p);
            PlayerNoteEvent event;
            event.midi_pitch = p;
            event.t_seconds = onset.t_seconds;
            events_.push_back(event);

            // Calibration: measure the strongest low partial's actual
            // frequency and fold the cents deviation into the global EMA and
            // the per-note profile.
            std::vector<double> peaks;
            std::vector<double> rises;
            score_pitch(p, before, after, background_per_bin, &peaks, &rises);
            const size_t count = std::min<size_t>(3, std::min(peaks.size(), rises.size()));
            size_t strongest = 0;
            for (size_t n = 1; n < count; ++n)
            {
                if (rises[n] > rises[strongest])
                    strongest = n;
            }
            if (count > 0)
            {
                const int n = static_cast<int>(strongest) + 1;
                const double b_coeff = inharmonicity_b(p);
                const double expected_fn = n * effective_frequency(p) * std::sqrt(1.0 + b_coeff * n * n);
                const double dev_cents = 1200.0 * std::log2(std::max(peaks[strongest], 1.0) / expected_fn);
                if (std::abs(dev_cents) <= tuning_.calibration_clamp_cents)
                {
                    global_offset_cents_ += tuning_.calibration_ema_global * dev_cents;
                    per_note_offset_cents_[p] += tuning_.calibration_ema_per_note * dev_cents * (1.0 - tuning_.calibration_ema_global);
                }
            }
        }
    }

    // Report confident unexpected notes (wrong notes must reach the judge),
    // with octave suppression: a shared-partial candidate needs enough
    // independent partial energy of its own. Suppression anchors on accepted
    // notes plus any expected note with a plausible score — an octave
    // phantom must not sneak through just because its fundamental was weak
    // at this particular onset.
    std::vector<int> suppression_base = accepted;
    for (const int p : distinct_expected)
    {
        if (p < low || p > high)
            continue;
        if (scores[static_cast<size_t>(p - low)] >= tuning_.suppression_expected_floor * best && std::find(suppression_base.begin(), suppression_base.end(), p) == suppression_base.end())
            suppression_base.push_back(p);
    }
    int reported = 0;
    for (int q = low; q <= high && reported < 3; ++q)
    {
        if (std::find(distinct_expected.begin(), distinct_expected.end(), q) != distinct_expected.end())
            continue;
        const double score = scores[static_cast<size_t>(q - low)];
        if (score < tuning_.wrong_relative * best)
            continue;

        bool suppressed = false;
        for (const int p : suppression_base)
        {
            bool shared_family = false;
            for (const int interval : kSharedPartialIntervals)
                shared_family |= (q - p) == interval;
            if (!shared_family)
                continue;
            if (independent_fraction(q, p, before, after, background_per_bin) < tuning_.independent_partial_fraction)
            {
                suppressed = true;
                break;
            }
        }
        if (suppressed)
            continue;

        PlayerNoteEvent event;
        event.midi_pitch = q;
        event.t_seconds = onset.t_seconds;
        events_.push_back(event);
        ++reported;
    }
}

std::vector<PlayerNoteEvent> NoteListener::take_events()
{
    std::vector<PlayerNoteEvent> out = std::move(events_);
    events_.clear();
    return out;
}

} // namespace scoreview
} // namespace draxul
