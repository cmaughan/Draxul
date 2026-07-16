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

// Classify a set of pitch classes; roots are tried bass-first so inversions
// name their true chord. Empty string when nothing matches.
std::string classify_chord(const std::set<int>& pcs, int bass_pc)
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
                return std::string(kPitchNames[root]) + " " + tmpl.quality;
        }
    }
    return {};
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

std::string key_name(int tonic_pc, bool minor)
{
    return std::string(kPitchNames[((tonic_pc % 12) + 12) % 12]) + (minor ? " minor" : " major");
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
        const std::string name = classify_chord(pcs, bass_pc);
        if (name.empty())
            continue;
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

    // --- Motifs: melodic interval n-grams --------------------------------
    std::vector<std::pair<double, int>> melody; // (qstamp, top pitch)
    for (const AnalysisOnset& onset : onsets)
    {
        if (!onset.pitches.empty())
            melody.emplace_back(
                onset.qstamp, *std::max_element(onset.pitches.begin(), onset.pitches.end()));
    }
    std::vector<int> intervals;
    for (size_t i = 1; i < melody.size(); ++i)
        intervals.push_back(melody[i].second - melody[i - 1].second);
    std::map<std::vector<int>, std::pair<int, double>> grams; // count, first_q
    for (size_t n = 4; n <= 8; ++n)
    {
        for (size_t i = 0; i + n <= intervals.size(); ++i)
        {
            const std::vector<int> gram(
                intervals.begin() + static_cast<long>(i), intervals.begin() + static_cast<long>(i + n));
            auto [it, inserted] = grams.emplace(gram, std::make_pair(0, melody[i].first));
            ++it->second.first;
        }
    }
    // Keep recurring maximal patterns: drop a gram when a longer kept gram
    // with the same count contains it.
    std::vector<PieceProfile::Motif> motifs;
    for (const auto& [gram, stats] : grams)
    {
        if (stats.first >= 3)
        {
            PieceProfile::Motif motif;
            motif.intervals = gram;
            motif.count = stats.first;
            motif.first_q = stats.second;
            motifs.push_back(std::move(motif));
        }
    }
    std::sort(motifs.begin(), motifs.end(), [](const auto& a, const auto& b) {
        return a.count != b.count ? a.count > b.count : a.intervals.size() > b.intervals.size();
    });
    const auto contains = [](const std::vector<int>& big, const std::vector<int>& small) {
        if (small.size() > big.size())
            return false;
        for (size_t at = 0; at + small.size() <= big.size(); ++at)
        {
            if (std::equal(small.begin(), small.end(), big.begin() + static_cast<long>(at)))
                return true;
        }
        return false;
    };
    for (const PieceProfile::Motif& motif : motifs)
    {
        bool subsumed = false;
        for (const PieceProfile::Motif& kept : profile.motifs)
        {
            if (kept.count == motif.count && contains(kept.intervals, motif.intervals))
            {
                subsumed = true;
                break;
            }
        }
        if (!subsumed)
            profile.motifs.push_back(motif);
        if (profile.motifs.size() >= 8)
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

    return profile;
}

std::string PieceProfile::serialize() const
{
    nlohmann::json doc;
    doc["key"] = { { "name", key_name(global_key.tonic_pc, global_key.minor) },
        { "confidence", global_key.confidence } };
    nlohmann::json sections = nlohmann::json::array();
    for (const KeySection& section : key_sections)
    {
        sections.push_back({ { "start_q", section.start_q }, { "end_q", section.end_q },
            { "name", key_name(section.key.tonic_pc, section.key.minor) },
            { "confidence", section.key.confidence } });
    }
    doc["key_sections"] = sections;

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
        motifs_json.push_back({ { "intervals", motif.intervals }, { "count", motif.count },
            { "first_q", motif.first_q } });
    }
    doc["motifs"] = motifs_json;

    nlohmann::json figures_json = nlohmann::json::array();
    for (const Figure& figure : figures)
    {
        figures_json.push_back({ { "signature", figure.signature }, { "name", figure.name },
            { "count", figure.count }, { "example_beats", figure.example_beats } });
    }
    doc["figures"] = figures_json;
    return doc.dump(2);
}

} // namespace scoreview
} // namespace draxul
