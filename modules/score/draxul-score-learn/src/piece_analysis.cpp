#include <draxul/scoreview/piece_analysis.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace draxul
{
namespace scoreview
{

namespace
{

// Krumhansl–Kessler key profiles (probe-tone ratings).
constexpr std::array<double, 12> kMajorProfile = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52,
    5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr std::array<double, 12> kMinorProfile = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54,
    4.75, 3.98, 2.69, 3.34, 3.17 };
constexpr double kSignaturePriorBonus = 0.03;
constexpr int kKeyWindowBars = 8;
constexpr int kKeyHopBars = 4;

constexpr const char* kPitchNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#",
    "A", "A#", "B" };

double pearson(const std::array<double, 12>& xs, const std::array<double, 12>& ys)
{
    double mx = 0.0;
    double my = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        mx += xs[i];
        my += ys[i];
    }
    mx /= 12.0;
    my /= 12.0;
    double sxy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        sxy += (xs[i] - mx) * (ys[i] - my);
        sxx += (xs[i] - mx) * (xs[i] - mx);
        syy += (ys[i] - my) * (ys[i] - my);
    }
    return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt(sxx * syy) : 0.0;
}

// Best key for a pitch-class histogram: correlate against all 24 rotations,
// with a small prior bonus for the keys the notated signature implies.
PieceProfile::KeyEstimate estimate_key(
    const std::array<double, 12>& histogram, std::optional<int> notated_fifths)
{
    int prior_major_pc = -1;
    int prior_minor_pc = -1;
    if (notated_fifths.has_value())
    {
        prior_major_pc = ((*notated_fifths * 7) % 12 + 12) % 12;
        prior_minor_pc = (prior_major_pc + 9) % 12;
    }

    PieceProfile::KeyEstimate best;
    double best_score = -2.0;
    double second_score = -2.0;
    for (int minor = 0; minor <= 1; ++minor)
    {
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            std::array<double, 12> rotated{};
            const auto& profile = minor != 0 ? kMinorProfile : kMajorProfile;
            for (int pc = 0; pc < 12; ++pc)
                rotated[pc] = profile[((pc - tonic) % 12 + 12) % 12];
            double score = pearson(histogram, rotated);
            if (minor == 0 && tonic == prior_major_pc)
                score += kSignaturePriorBonus;
            if (minor != 0 && tonic == prior_minor_pc)
                score += kSignaturePriorBonus;
            if (score > best_score)
            {
                second_score = best_score;
                best_score = score;
                best.tonic_pc = tonic;
                best.minor = minor != 0;
            }
            else if (score > second_score)
            {
                second_score = score;
            }
        }
    }
    best.confidence = best_score - second_score;
    return best;
}

std::array<double, 12> weighted_histogram(
    const std::vector<AnalysisOnset>& onsets, size_t begin, size_t end)
{
    std::array<double, 12> histogram{};
    for (size_t i = begin; i < end && i < onsets.size(); ++i)
    {
        // IOI weight as a duration proxy: longer notes shape the key more.
        const double next_q = i + 1 < onsets.size() ? onsets[i + 1].qstamp : onsets[i].qstamp + 1.0;
        const double weight = std::clamp(next_q - onsets[i].qstamp, 0.1, 2.0);
        for (const int pitch : onsets[i].pitches)
            histogram[((pitch % 12) + 12) % 12] += weight;
    }
    return histogram;
}

// Chord templates, relative pitch classes from the root.
struct ChordTemplate
{
    const char* quality;
    std::vector<int> pcs;
};
const std::vector<ChordTemplate>& chord_templates()
{
    static const std::vector<ChordTemplate> templates = {
        { "dom7", { 0, 4, 7, 10 } },
        { "maj7", { 0, 4, 7, 11 } },
        { "min7", { 0, 3, 7, 10 } },
        { "halfdim7", { 0, 3, 6, 10 } },
        { "dim7", { 0, 3, 6, 9 } },
        { "maj", { 0, 4, 7 } },
        { "min", { 0, 3, 7 } },
        { "dim", { 0, 3, 6 } },
        { "aug", { 0, 4, 8 } },
    };
    return templates;
}

// A classified sonority. The root is kept alongside the display name because
// cadence detection reasons about scale degrees, not strings.
struct ChordId
{
    int root_pc = -1;
    const char* quality = nullptr;

    bool valid() const
    {
        return root_pc >= 0 && quality != nullptr;
    }
    std::string name() const
    {
        return std::string(kPitchNames[root_pc]) + " " + quality;
    }
};

// Classify a set of pitch classes; roots are tried bass-first so inversions
// name their true chord. Invalid when nothing matches.
ChordId classify_chord(const std::set<int>& pcs, int bass_pc)
{
    if (pcs.size() < 3)
        return {};
    std::vector<int> roots;
    roots.push_back(bass_pc);
    for (const int pc : pcs)
    {
        if (pc != bass_pc)
            roots.push_back(pc);
    }
    for (const ChordTemplate& tmpl : chord_templates())
    {
        for (const int root : roots)
        {
            bool contains_all = true;
            for (const int rel : tmpl.pcs)
            {
                if (pcs.count((root + rel) % 12) == 0)
                {
                    contains_all = false;
                    break;
                }
            }
            // Exact template coverage: no unexplained pitch classes.
            if (contains_all && pcs.size() == tmpl.pcs.size())
                return ChordId{ root, tmpl.quality };
        }
    }
    return {};
}

// --- Structure detection (C0) ------------------------------------------
// Phrase boundaries are scored from four independent musical cues, combined
// as a probabilistic OR so no single cue can exceed certainty.
//
// Only EVIDENCE opens a phrase. The hypermetric prior never does: it merely
// breaks ties when an over-long phrase must be split somewhere. Otherwise a
// 4-bar guess plus any faint cue would manufacture phrasing out of nothing,
// and the composer would trust it.
//
// The thresholds are set so that no single WEAK cue suffices — a motif
// restarting on a barline happens mid-phrase all the time — while a real
// cadence, a modulation, or genuine breathing space each stand alone.
constexpr double kBoundaryThreshold = 0.55;
constexpr int kMinPhraseBars = 2;
constexpr int kMaxPhraseBars = 16;
constexpr double kPriorFourBar = 0.25;
constexpr double kPriorTwoBar = 0.10;
constexpr double kScoreKeyChange = 0.9; // a modulation IS a boundary
constexpr double kScoreMotifRestart = 0.45; // reinforces; never fires alone
constexpr double kScoreHalfCadence = 0.5; // reinforces; never fires alone
constexpr double kScoreBareTonic = 0.2;
constexpr double kQEpsilon = 1e-6;

double combine(std::initializer_list<double> scores)
{
    double miss = 1.0;
    for (const double score : scores)
        miss *= 1.0 - std::clamp(score, 0.0, 1.0);
    return 1.0 - miss;
}

double hypermetric_prior(int bar)
{
    if (bar % 4 == 0)
        return kPriorFourBar;
    if (bar % 2 == 0)
        return kPriorTwoBar;
    return 0.0;
}

double median_ioi(const std::vector<double>& qs)
{
    std::vector<double> gaps;
    gaps.reserve(qs.size());
    for (size_t i = 1; i < qs.size(); ++i)
    {
        const double gap = qs[i] - qs[i - 1];
        if (gap > kQEpsilon)
            gaps.push_back(gap);
    }
    if (gaps.empty())
        return 1.0;
    std::nth_element(gaps.begin(), gaps.begin() + static_cast<long>(gaps.size() / 2), gaps.end());
    return std::max(kQEpsilon, gaps[gaps.size() / 2]);
}

// Breathing space across the barline. With onsets alone a rest and a long
// held note look identical — and musically both end a phrase, so we neither
// can nor need to tell them apart.
double gap_score(const std::vector<double>& qs, double boundary_q, double median)
{
    const auto next = std::lower_bound(qs.begin(), qs.end(), boundary_q - kQEpsilon);
    if (next == qs.begin() || next == qs.end())
        return 0.0;
    const double gap = *next - *(next - 1);
    return std::clamp((gap / median - 1.0) / 3.0, 0.0, 1.0);
}

// A phrase ends with a cadence, so evidence for a boundary at bar b lives in
// bar b-1: dominant resolving to tonic (authentic) or a bar left hanging on
// the dominant (half).
double cadence_score(const std::vector<std::vector<int>>& bar_roots, int prev_bar, int tonic_pc)
{
    if (prev_bar < 0 || prev_bar >= static_cast<int>(bar_roots.size()))
        return 0.0;
    const std::vector<int>& roots = bar_roots[static_cast<size_t>(prev_bar)];
    if (roots.empty())
        return 0.0;
    const int dominant = (tonic_pc + 7) % 12;
    if (roots.back() == tonic_pc)
    {
        for (size_t i = 0; i + 1 < roots.size(); ++i)
        {
            if (roots[i] == dominant && roots[i + 1] == tonic_pc)
                return 1.0;
        }
        // The dominant may sit across the barline, in the bar before.
        if (prev_bar > 0)
        {
            const std::vector<int>& before = bar_roots[static_cast<size_t>(prev_bar) - 1];
            if (!before.empty() && before.back() == dominant && roots.front() == tonic_pc)
                return 1.0;
        }
        return kScoreBareTonic; // a tonic with no dominant preparing it
    }
    if (roots.back() == dominant)
        return kScoreHalfCadence;
    return 0.0;
}

double key_change_score(const std::vector<PieceProfile::KeySection>& sections, double boundary_q)
{
    for (size_t i = 1; i < sections.size(); ++i)
    {
        if (std::abs(sections[i].start_q - boundary_q) < kQEpsilon)
            return kScoreKeyChange;
    }
    return 0.0;
}

// A known motif starting exactly on the barline marks a phrase opening.
double motif_score(const std::vector<std::pair<double, int>>& melody,
    const std::vector<int>& intervals, const std::vector<PieceProfile::Motif>& motifs,
    double boundary_q)
{
    size_t at = 0;
    while (at < melody.size() && melody[at].first < boundary_q - kQEpsilon)
        ++at;
    if (at >= melody.size() || melody[at].first > boundary_q + kQEpsilon)
        return 0.0;
    for (const PieceProfile::Motif& motif : motifs)
    {
        if (at + motif.intervals.size() > intervals.size())
            continue;
        if (std::equal(motif.intervals.begin(), motif.intervals.end(),
                intervals.begin() + static_cast<long>(at)))
            return kScoreMotifRestart;
    }
    return 0.0;
}

std::string figure_name(const std::string& signature)
{
    static const std::map<std::string, std::string> names = {
        { "0", "quarter" },
        { "0,6", "eighths" },
        { "0,4,8", "triplet" },
        { "0,3,6,9", "sixteenths" },
        { "0,9", "dotted-eighth+16th" },
        { "0,3", "16th-pickup" },
        { "0,8", "triplet(2+1)" },
        { "0,4", "triplet(1+2)" },
        { "0,6,9", "eighth+two-16ths" },
        { "0,3,6", "two-16ths+eighth" },
    };
    const auto found = names.find(signature);
    return found != names.end() ? found->second : signature;
}

} // namespace

std::vector<MelodyNote> skyline_melody(const std::vector<AnalysisOnset>& onsets)
{
    // Afterglow: for a short window after a melody note ends, onsets far
    // below it are still accompaniment (the waltz bass striking in a
    // half-beat melody breath), not a melodic leap. A genuine register
    // change (within an octave) or anything after a real rest passes.
    constexpr double kAfterglowQ = 1.0;
    constexpr int kAfterglowDropSemitones = 12;
    std::vector<MelodyNote> melody;
    melody.reserve(onsets.size());
    int ring_pitch = -1;
    double ring_end = -1.0;
    for (size_t oi = 0; oi < onsets.size(); ++oi)
    {
        const AnalysisOnset& onset = onsets[oi];
        if (onset.pitches.empty())
            continue;
        size_t top = 0;
        for (size_t vi = 1; vi < onset.pitches.size(); ++vi)
        {
            if (onset.pitches[vi] > onset.pitches[top])
                top = vi;
        }
        // Shadowed: the previous melody note is still sounding above this
        // onset's top. Strictly-below keeps same-pitch retriggers audible.
        if (onset.qstamp + kQEpsilon < ring_end && onset.pitches[top] < ring_pitch)
            continue;
        if (onset.qstamp + kQEpsilon < ring_end + kAfterglowQ
            && onset.pitches[top] < ring_pitch - kAfterglowDropSemitones)
            continue;
        MelodyNote note;
        note.qstamp = onset.qstamp;
        note.pitch = onset.pitches[top];
        note.onset = oi;
        note.voice = top;
        melody.push_back(note);
        ring_pitch = note.pitch;
        const double duration = top < onset.durations.size() ? onset.durations[top] : 0.0;
        ring_end = onset.qstamp + std::max(0.0, duration);
    }
    return melody;
}

std::string key_name(int tonic_pc, bool minor)
{
    return std::string(kPitchNames[((tonic_pc % 12) + 12) % 12]) + (minor ? " minor" : " major");
}

std::string note_name(int midi)
{
    if (midi < 0)
        return "?";
    return std::string(kPitchNames[((midi % 12) + 12) % 12]) + std::to_string(midi / 12 - 1);
}

PieceProfile analyze_piece(const std::vector<AnalysisOnset>& onsets, double quarters_per_bar,
    std::optional<int> notated_fifths)
{
    PieceProfile profile;
    if (onsets.empty())
        return profile;
    const double bar_q = quarters_per_bar > 0.0 ? quarters_per_bar : 4.0;

    // --- Key: global + windowed sections -------------------------------
    profile.global_key = estimate_key(weighted_histogram(onsets, 0, onsets.size()), notated_fifths);

    const double total_q = onsets.back().qstamp + 1.0;
    const int total_bars = static_cast<int>(std::ceil(total_q / bar_q));
    std::vector<PieceProfile::KeySection> raw_sections;
    for (int bar = 0; bar < total_bars; bar += kKeyHopBars)
    {
        const double start_q = bar * bar_q;
        const double end_q = std::min(total_q, (bar + kKeyWindowBars) * bar_q);
        size_t begin = 0;
        while (begin < onsets.size() && onsets[begin].qstamp < start_q)
            ++begin;
        size_t end = begin;
        while (end < onsets.size() && onsets[end].qstamp < end_q)
            ++end;
        if (end - begin < 4)
            continue;
        PieceProfile::KeySection section;
        section.start_q = start_q;
        section.end_q = end_q;
        section.key = estimate_key(weighted_histogram(onsets, begin, end), notated_fifths);
        raw_sections.push_back(section);
    }
    // Merge consecutive windows agreeing on the key; only report sections
    // when the piece actually moves somewhere.
    for (const PieceProfile::KeySection& section : raw_sections)
    {
        if (!profile.key_sections.empty()
            && profile.key_sections.back().key.tonic_pc == section.key.tonic_pc
            && profile.key_sections.back().key.minor == section.key.minor)
        {
            profile.key_sections.back().end_q = section.end_q;
            profile.key_sections.back().key.confidence = std::max(profile.key_sections.back().key.confidence, section.key.confidence);
        }
        else
        {
            profile.key_sections.push_back(section);
        }
    }
    if (profile.key_sections.size() <= 1)
        profile.key_sections.clear();

    // --- Chords + transitions -------------------------------------------
    // Waltz reality: the bass lands on beat 1 and the upper dyads follow;
    // remember the bar's lowest opening pitch as the harmonic bass.
    std::map<std::string, int> chord_counts;
    std::map<std::string, std::map<std::string, int>> chord_next;
    std::string previous_chord;
    int bar_bass_pc = -1;
    int bar_bass_bar = -1;
    // Chord roots per bar, in order — the raw material cadence detection
    // needs (structure, below), collected while we are already classifying.
    std::vector<std::vector<int>> bar_roots(static_cast<size_t>(std::max(0, total_bars)));
    for (const AnalysisOnset& onset : onsets)
    {
        if (onset.pitches.empty())
            continue;
        const int bar = static_cast<int>(onset.qstamp / bar_q);
        const int lowest = *std::min_element(onset.pitches.begin(), onset.pitches.end());
        if (bar != bar_bass_bar)
        {
            bar_bass_bar = bar;
            bar_bass_pc = ((lowest % 12) + 12) % 12;
        }
        std::set<int> pcs;
        for (const int pitch : onset.pitches)
            pcs.insert(((pitch % 12) + 12) % 12);
        int bass_pc = ((lowest % 12) + 12) % 12;
        if (pcs.size() == 2 && bar_bass_pc >= 0)
        {
            pcs.insert(bar_bass_pc); // dyads borrow the bar's bass
            bass_pc = bar_bass_pc;
        }
        const ChordId chord = classify_chord(pcs, bass_pc);
        if (!chord.valid())
            continue;
        const std::string name = chord.name();
        if (bar >= 0 && bar < total_bars)
            bar_roots[static_cast<size_t>(bar)].push_back(chord.root_pc);
        ++chord_counts[name];
        if (!previous_chord.empty() && previous_chord != name)
            ++chord_next[previous_chord][name];
        previous_chord = name;
    }
    for (const auto& [name, count] : chord_counts)
    {
        PieceProfile::Chord chord;
        chord.name = name;
        chord.count = count;
        std::vector<std::pair<std::string, int>> next(
            chord_next[name].begin(), chord_next[name].end());
        std::sort(next.begin(), next.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        if (next.size() > 3)
            next.resize(3);
        chord.next = std::move(next);
        profile.chords.push_back(std::move(chord));
    }
    std::sort(profile.chords.begin(), profile.chords.end(),
        [](const auto& a, const auto& b) { return a.count > b.count; });

    // --- Motifs -----------------------------------------------------------
    // The melody line via the sustain-aware skyline (see skyline_melody);
    // kept as (qstamp, pitch, duration) triples for the miner's rhythm and
    // silence handling.
    std::vector<std::pair<double, int>> melody; // (qstamp, pitch)
    std::vector<double> melody_durations;
    for (const MelodyNote& note : skyline_melody(onsets))
    {
        melody.emplace_back(note.qstamp, note.pitch);
        melody_durations.push_back(note.voice < onsets[note.onset].durations.size()
                ? onsets[note.onset].durations[note.voice]
                : 0.0);
    }
    std::vector<int> intervals;
    for (size_t i = 1; i < melody.size(); ++i)
        intervals.push_back(melody[i].second - melody[i - 1].second);
    // Motif mining: a bounded-depth suffix trie over (interval, step-rhythm)
    // tokens. Every melody position inserts its forward path (up to
    // kMotifMaxSteps edges); a node's occurrence count is how often that
    // exact pitch+rhythm pattern appears. A motif is a MAXIMAL
    // well-supported path:
    //   - deep enough to mean something (kMotifMinSteps..kMotifMaxSteps
    //     steps = 4..8 notes — a motif is music's smallest meaningful unit),
    //   - recurring (count >= kMotifMinCount),
    //   - right-maximal: no continuation keeps the full count (the
    //     occurrences diverge here — this is where the motif ends),
    //   - left-maximal: its occurrences are preceded by different tokens
    //     (otherwise the true motif starts earlier; the deeper path wins).
    // Sequences break at rests (a step longer than kMotifBreakTwelfths), so
    // a motif never straddles a breath.
    constexpr int kMotifMinSteps = 3; // 4 notes
    constexpr int kMotifMaxSteps = 7; // 8 notes
    constexpr int kMotifMinCount = 3;
    constexpr size_t kMotifKeep = 8;
    // A motif never crosses a real breath: break where the SILENCE after a
    // note ends exceeds a quarter. With durations unknown the note is
    // treated as instantaneous, so the whole inter-onset gap counts —
    // uniform lines still chain, true rests still break.
    constexpr int kMotifBreakSilenceTwelfths = 12;

    struct Token
    {
        int interval = 0;
        int ioi_twelfths = 0;
        bool breaks = false; // a rest: no motif may cross it

        uint32_t key() const
        {
            return static_cast<uint32_t>(interval + 512)
                | (static_cast<uint32_t>(ioi_twelfths) << 16);
        }
    };
    std::vector<Token> tokens;
    tokens.reserve(intervals.size());
    for (size_t i = 0; i + 1 < melody.size(); ++i)
    {
        Token token;
        token.interval = melody[i + 1].second - melody[i].second;
        token.ioi_twelfths = static_cast<int>(std::clamp<long>(
            std::lround((melody[i + 1].first - melody[i].first) * 12.0), 1, 4095));
        const double silence_q
            = (melody[i + 1].first - melody[i].first) - std::max(0.0, melody_durations[i]);
        token.breaks
            = std::lround(silence_q * 12.0) > kMotifBreakSilenceTwelfths;
        tokens.push_back(token);
    }

    struct TrieNode
    {
        std::map<uint32_t, int> children;
        std::vector<int> starts; // melody indices whose path passes here
    };
    std::vector<TrieNode> trie(1);
    for (size_t s0 = 0; s0 < tokens.size(); ++s0)
    {
        if (tokens[s0].breaks)
            continue;
        int node = 0;
        for (size_t d = 0; d < static_cast<size_t>(kMotifMaxSteps) && s0 + d < tokens.size();
            ++d)
        {
            const Token& token = tokens[s0 + d];
            if (token.breaks)
                break;
            auto [it, fresh] = trie[static_cast<size_t>(node)].children.try_emplace(
                token.key(), static_cast<int>(trie.size()));
            // Growing the trie can reallocate the vector and move the map that
            // owns `it`. Preserve the child index before emplacing a node so we
            // never dereference an invalidated iterator.
            const int child = it->second;
            if (fresh)
                trie.emplace_back();
            node = child;
            trie[static_cast<size_t>(node)].starts.push_back(static_cast<int>(s0));
        }
    }

    // DFS with the path in hand; emit maximal qualifying nodes.
    std::vector<PieceProfile::Motif> motifs;
    struct Frame
    {
        int node = 0;
        std::map<uint32_t, int>::const_iterator next;
    };
    std::vector<Frame> stack{ { 0, trie[0].children.begin() } };
    std::vector<Token> path;
    while (!stack.empty())
    {
        Frame& frame = stack.back();
        if (frame.next == trie[static_cast<size_t>(frame.node)].children.end())
        {
            const TrieNode& node = trie[static_cast<size_t>(frame.node)];
            const int depth = static_cast<int>(path.size());
            const int count = static_cast<int>(node.starts.size());
            if (depth >= kMotifMinSteps && count >= kMotifMinCount)
            {
                bool right_maximal = depth == kMotifMaxSteps;
                if (!right_maximal)
                {
                    right_maximal = true;
                    for (const auto& [key, child] : node.children)
                    {
                        if (static_cast<int>(trie[static_cast<size_t>(child)].starts.size())
                            == count)
                        {
                            right_maximal = false;
                            break;
                        }
                    }
                }
                bool left_maximal = false;
                if (right_maximal)
                {
                    // Left-maximal: some occurrence opens a sequence, or the
                    // predecessors differ. All-same-predecessor means the
                    // real motif starts one note earlier — unless that would
                    // exceed the depth cap, in which case accept here.
                    if (depth == kMotifMaxSteps)
                    {
                        left_maximal = true;
                    }
                    else
                    {
                        uint32_t shared = 0;
                        bool first = true;
                        for (const int s0 : node.starts)
                        {
                            if (s0 == 0 || tokens[static_cast<size_t>(s0) - 1].breaks)
                            {
                                left_maximal = true;
                                break;
                            }
                            const uint32_t prev = tokens[static_cast<size_t>(s0) - 1].key();
                            if (first)
                            {
                                shared = prev;
                                first = false;
                            }
                            else if (prev != shared)
                            {
                                left_maximal = true;
                                break;
                            }
                        }
                    }
                }
                if (right_maximal && left_maximal)
                {
                    PieceProfile::Motif motif;
                    for (const Token& token : path)
                    {
                        motif.intervals.push_back(token.interval);
                        motif.ioi_twelfths.push_back(token.ioi_twelfths);
                    }
                    motif.count = count;
                    for (const int s0 : node.starts)
                        motif.occurrences_q.push_back(melody[static_cast<size_t>(s0)].first);
                    motif.first_q = motif.occurrences_q.front();
                    motifs.push_back(std::move(motif));
                }
            }
            stack.pop_back();
            if (!path.empty())
                path.pop_back();
            continue;
        }
        const auto child = frame.next++;
        Token token;
        token.interval = static_cast<int>(child->first & 0xffffu) - 512;
        token.ioi_twelfths = static_cast<int>(child->first >> 16);
        path.push_back(token);
        stack.push_back(
            { child->second, trie[static_cast<size_t>(child->second)].children.begin() });
    }

    // Strongest first; at equal strength prefer longer, then EARLIEST — the
    // piece's opening gesture should win ties, not lose them.
    std::sort(motifs.begin(), motifs.end(), [](const auto& a, const auto& b) {
        if (a.count != b.count)
            return a.count > b.count;
        if (a.intervals.size() != b.intervals.size())
            return a.intervals.size() > b.intervals.size();
        return a.first_q < b.first_q;
    });
    // Collapse shift families: a long repeating phrase reaches the depth cap
    // as MANY overlapping windows, each occurring at a constant offset from
    // its siblings — one underlying pattern must not flood every kept slot.
    // Two motifs are the same family when their occurrence lists align under
    // one constant time shift; the sort order keeps the earliest window.
    for (const PieceProfile::Motif& motif : motifs)
    {
        bool shifted_duplicate = false;
        for (const PieceProfile::Motif& kept : profile.motifs)
        {
            if (kept.occurrences_q.size() != motif.occurrences_q.size())
                continue;
            const double delta = motif.occurrences_q.front() - kept.occurrences_q.front();
            bool constant = true;
            for (size_t i = 1; i < motif.occurrences_q.size() && constant; ++i)
                constant = std::abs(
                               (motif.occurrences_q[i] - kept.occurrences_q[i]) - delta)
                    < 1e-6;
            if (constant)
            {
                shifted_duplicate = true;
                break;
            }
        }
        if (!shifted_duplicate)
            profile.motifs.push_back(motif);
        if (profile.motifs.size() >= kMotifKeep)
            break;
    }

    // --- Rhythm figures: per-beat onset patterns on a twelfth grid -------
    std::map<std::string, PieceProfile::Figure> figures;
    std::map<int, std::vector<int>> beat_offsets;
    for (const AnalysisOnset& onset : onsets)
    {
        const int beat = static_cast<int>(std::floor(onset.qstamp));
        const int twelfth = static_cast<int>(std::lround((onset.qstamp - beat) * 12.0)) % 12;
        beat_offsets[beat].push_back(twelfth);
    }
    for (auto& [beat, offsets] : beat_offsets)
    {
        std::sort(offsets.begin(), offsets.end());
        offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
        std::string signature;
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            if (i > 0)
                signature += ',';
            signature += std::to_string(offsets[i]);
        }
        PieceProfile::Figure& figure = figures[signature];
        figure.signature = signature;
        figure.name = figure_name(signature);
        ++figure.count;
        if (figure.example_beats.size() < 3)
            figure.example_beats.push_back(static_cast<double>(beat));
    }
    for (auto& [signature, figure] : figures)
    {
        if (figure.count >= 2)
            profile.figures.push_back(std::move(figure));
    }
    std::sort(profile.figures.begin(), profile.figures.end(),
        [](const auto& a, const auto& b) { return a.count > b.count; });

    // --- Structure: phrases + sections (C0) ------------------------------
    // Runs last: it feeds on the key sections, chord roots and motifs the
    // passes above produced.
    if (total_bars > 0)
    {
        std::vector<double> qs;
        qs.reserve(onsets.size());
        for (const AnalysisOnset& onset : onsets)
            qs.push_back(onset.qstamp);
        const double median = median_ioi(qs);

        // Score every interior bar as a candidate phrase start.
        std::vector<double> evidence(static_cast<size_t>(total_bars), 0.0);
        std::vector<double> score(static_cast<size_t>(total_bars), 0.0);
        for (int bar = 1; bar < total_bars; ++bar)
        {
            const double boundary_q = bar * bar_q;
            // The local key decides what counts as a cadence here.
            PieceProfile::KeyEstimate local = profile.global_key;
            for (const PieceProfile::KeySection& section : profile.key_sections)
            {
                if (boundary_q >= section.start_q && boundary_q < section.end_q)
                {
                    local = section.key;
                    break;
                }
            }
            const double found = combine({ gap_score(qs, boundary_q, median),
                cadence_score(bar_roots, bar - 1, local.tonic_pc),
                key_change_score(profile.key_sections, boundary_q),
                motif_score(melody, intervals, profile.motifs, boundary_q) });
            evidence[static_cast<size_t>(bar)] = found;
            score[static_cast<size_t>(bar)] = std::min(1.0, found + hypermetric_prior(bar));
        }

        // Evidence alone opens a phrase — never the prior (see above).
        std::vector<int> starts{ 0 };
        for (int bar = kMinPhraseBars; bar < total_bars; ++bar)
        {
            if (evidence[static_cast<size_t>(bar)] >= kBoundaryThreshold
                && bar - starts.back() >= kMinPhraseBars)
                starts.push_back(bar);
        }
        // Anything longer than the maximum splits at its best interior
        // candidate: a piece with no cadences still yields workable chunks.
        // A forced cut is an admission that we had to chunk SOMEWHERE, not a
        // boundary we detected, so it carries no confidence even when it
        // happens to land on a faint cue.
        std::vector<char> forced(static_cast<size_t>(total_bars), 0);
        for (bool split = true; split;)
        {
            split = false;
            for (size_t i = 0; i < starts.size(); ++i)
            {
                const int begin = starts[i];
                const int end = i + 1 < starts.size() ? starts[i + 1] : total_bars;
                if (end - begin <= kMaxPhraseBars)
                    continue;
                int best = -1;
                double best_score = -1.0;
                for (int bar = begin + kMinPhraseBars; bar <= end - kMinPhraseBars; ++bar)
                {
                    if (score[static_cast<size_t>(bar)] > best_score)
                    {
                        best_score = score[static_cast<size_t>(bar)];
                        best = bar;
                    }
                }
                if (best < 0)
                    break;
                forced[static_cast<size_t>(best)] = 1;
                starts.insert(starts.begin() + static_cast<long>(i) + 1, best);
                split = true;
                break;
            }
        }

        for (size_t i = 0; i < starts.size(); ++i)
        {
            PieceProfile::Phrase phrase;
            phrase.start_bar = starts[i];
            phrase.end_bar = i + 1 < starts.size() ? starts[i + 1] : total_bars;
            phrase.start_q = phrase.start_bar * bar_q;
            phrase.end_q = phrase.end_bar * bar_q;
            // The opening phrase has no boundary evidence (nothing precedes
            // it to cadence), and a forced cut has none by definition.
            phrase.confidence = phrase.start_bar > 0 && !forced[static_cast<size_t>(phrase.start_bar)]
                ? evidence[static_cast<size_t>(phrase.start_bar)]
                : 0.0;
            profile.phrases.push_back(phrase);
        }
        double sum = 0.0;
        int counted = 0;
        for (const PieceProfile::Phrase& phrase : profile.phrases)
        {
            if (phrase.start_bar > 0)
            {
                sum += phrase.confidence;
                ++counted;
            }
        }
        profile.structure_confidence = counted > 0 ? sum / counted : 0.0;

        // Sections group phrases, and only split where the key actually
        // moves — an unmodulating piece is honestly one section.
        std::vector<int> section_starts{ 0 };
        for (size_t i = 1; i < profile.phrases.size(); ++i)
        {
            if (key_change_score(profile.key_sections, profile.phrases[i].start_q) > 0.0)
                section_starts.push_back(static_cast<int>(i));
        }
        for (size_t s = 0; s < section_starts.size(); ++s)
        {
            const int first = section_starts[s];
            const int last = s + 1 < section_starts.size() ? section_starts[s + 1]
                                                           : static_cast<int>(profile.phrases.size());
            PieceProfile::Section section;
            section.start_bar = profile.phrases[static_cast<size_t>(first)].start_bar;
            section.end_bar = profile.phrases[static_cast<size_t>(last) - 1].end_bar;
            section.start_q = profile.phrases[static_cast<size_t>(first)].start_q;
            section.end_q = profile.phrases[static_cast<size_t>(last) - 1].end_q;
            for (int p = first; p < last; ++p)
                section.phrase_ids.push_back(p);
            profile.sections.push_back(std::move(section));
        }

        profile.bar_positions.assign(static_cast<size_t>(total_bars), PieceProfile::BarPosition{});
        for (size_t s = 0; s < profile.sections.size(); ++s)
        {
            const PieceProfile::Section& section = profile.sections[s];
            for (const int p : section.phrase_ids)
            {
                const PieceProfile::Phrase& phrase = profile.phrases[static_cast<size_t>(p)];
                for (int bar = phrase.start_bar; bar < phrase.end_bar && bar < total_bars; ++bar)
                {
                    PieceProfile::BarPosition& pos = profile.bar_positions[static_cast<size_t>(bar)];
                    pos.section_id = static_cast<int>(s);
                    pos.phrase_id = p;
                    pos.serial_in_phrase = bar - phrase.start_bar;
                    pos.phrase_length = phrase.end_bar - phrase.start_bar;
                    pos.phrase_open = bar == phrase.start_bar;
                    pos.section_open = bar == section.start_bar;
                }
            }
        }

        // --- Phrase restatements --------------------------------------
        // Music restates itself; a phrase whose melodic shape already
        // appeared is not new material. The signature is the top voice's
        // interval sequence plus its rhythm (onset offsets from the phrase
        // start, on the twelfth grid) — transposition-invariant by
        // construction, so a restatement up a fifth still matches; the
        // opening pitch distinguishes a literal repeat from a transposed one.
        struct PhraseShape
        {
            std::vector<int> intervals;
            std::vector<long> offsets_twelfths;
            int first_pitch = -1;
            int bars = 0;
        };
        std::vector<PhraseShape> shapes;
        shapes.reserve(profile.phrases.size());
        for (const PieceProfile::Phrase& phrase : profile.phrases)
        {
            PhraseShape shape;
            shape.bars = phrase.end_bar - phrase.start_bar;
            int previous = -1;
            for (const auto& [q, pitch] : melody)
            {
                if (q < phrase.start_q - kQEpsilon || q >= phrase.end_q - kQEpsilon)
                    continue;
                shape.offsets_twelfths.push_back(
                    std::lround((q - phrase.start_q) * 12.0));
                if (shape.first_pitch < 0)
                    shape.first_pitch = pitch;
                if (previous >= 0)
                    shape.intervals.push_back(pitch - previous);
                previous = pitch;
            }
            shapes.push_back(std::move(shape));
        }
        for (size_t later = 1; later < shapes.size(); ++later)
        {
            // A trivial shape (too few notes) would "match" everything.
            if (shapes[later].intervals.size() < 3)
                continue;
            for (size_t earlier = 0; earlier < later; ++earlier)
            {
                if (shapes[earlier].bars != shapes[later].bars
                    || shapes[earlier].intervals != shapes[later].intervals
                    || shapes[earlier].offsets_twelfths != shapes[later].offsets_twelfths)
                    continue;
                // Chain restatements back to the ORIGINAL statement, and
                // judge transposition against that root.
                int root = static_cast<int>(earlier);
                if (profile.phrases[static_cast<size_t>(root)].repeat_of >= 0)
                    root = profile.phrases[static_cast<size_t>(root)].repeat_of;
                profile.phrases[later].repeat_of = root;
                profile.phrases[later].transposed = shapes[later].first_pitch
                    != shapes[static_cast<size_t>(root)].first_pitch;
                break;
            }
        }
    }

    return profile;
}

const PieceProfile::Phrase* PieceProfile::phrase_at_bar(int bar) const
{
    if (bar < 0 || bar >= static_cast<int>(bar_positions.size()))
        return nullptr;
    const int id = bar_positions[static_cast<size_t>(bar)].phrase_id;
    if (id < 0 || id >= static_cast<int>(phrases.size()))
        return nullptr;
    return &phrases[static_cast<size_t>(id)];
}

double PieceProfile::bar_tail_fraction(int bar) const
{
    if (bar < 0 || bar >= static_cast<int>(bar_positions.size()))
        return 0.0;
    const BarPosition& pos = bar_positions[static_cast<size_t>(bar)];
    if (pos.phrase_length <= 1)
        return 0.0;
    return static_cast<double>(pos.serial_in_phrase) / static_cast<double>(pos.phrase_length - 1);
}

std::string PieceProfile::serialize() const
{
    nlohmann::json doc;
    doc["key"] = { { "name", key_name(global_key.tonic_pc, global_key.minor) },
        { "confidence", global_key.confidence } };
    nlohmann::json key_sections_json = nlohmann::json::array();
    for (const KeySection& section : key_sections)
    {
        key_sections_json.push_back({ { "start_q", section.start_q }, { "end_q", section.end_q },
            { "name", key_name(section.key.tonic_pc, section.key.minor) },
            { "confidence", section.key.confidence } });
    }
    doc["key_sections"] = key_sections_json;

    nlohmann::json chords_json = nlohmann::json::array();
    for (const Chord& chord : chords)
    {
        nlohmann::json next = nlohmann::json::array();
        for (const auto& [name, count] : chord.next)
            next.push_back({ { "name", name }, { "count", count } });
        chords_json.push_back(
            { { "name", chord.name }, { "count", chord.count }, { "next", next } });
    }
    doc["chords"] = chords_json;

    nlohmann::json motifs_json = nlohmann::json::array();
    for (const Motif& motif : motifs)
    {
        motifs_json.push_back({ { "intervals", motif.intervals },
            { "ioi_twelfths", motif.ioi_twelfths }, { "count", motif.count },
            { "first_q", motif.first_q }, { "occurrences_q", motif.occurrences_q } });
    }
    doc["motifs"] = motifs_json;

    nlohmann::json figures_json = nlohmann::json::array();
    for (const Figure& figure : figures)
    {
        figures_json.push_back({ { "signature", figure.signature }, { "name", figure.name },
            { "count", figure.count }, { "example_beats", figure.example_beats } });
    }
    doc["figures"] = figures_json;

    nlohmann::json phrases_json = nlohmann::json::array();
    for (const Phrase& phrase : phrases)
    {
        phrases_json.push_back({ { "start_bar", phrase.start_bar }, { "end_bar", phrase.end_bar },
            { "start_q", phrase.start_q }, { "end_q", phrase.end_q },
            { "confidence", phrase.confidence }, { "repeat_of", phrase.repeat_of },
            { "transposed", phrase.transposed } });
    }
    doc["phrases"] = phrases_json;

    nlohmann::json sections_json = nlohmann::json::array();
    for (const Section& section : sections)
    {
        sections_json.push_back({ { "start_bar", section.start_bar },
            { "end_bar", section.end_bar }, { "start_q", section.start_q },
            { "end_q", section.end_q }, { "phrases", section.phrase_ids } });
    }
    doc["sections"] = sections_json;
    doc["structure_confidence"] = structure_confidence;
    return doc.dump(2);
}

} // namespace scoreview
} // namespace draxul
