#include <draxul/scoreview/measure_xml.h>

#include <algorithm>

namespace draxul
{
namespace scoreview
{

namespace
{

constexpr const char* kStepNames[12] = { "C", "C", "D", "D", "E", "F", "F", "G", "G", "A",
    "A", "B" };
constexpr int kStepAlters[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

} // namespace

std::string pitch_xml(int midi)
{
    const int pc = ((midi % 12) + 12) % 12;
    std::string out = "<pitch><step>";
    out += kStepNames[pc];
    out += "</step>";
    if (kStepAlters[pc] != 0)
        out += "<alter>1</alter>";
    out += "<octave>" + std::to_string(midi / 12 - 1) + "</octave></pitch>";
    return out;
}

std::string note_xml(int midi, int duration, const char* type, int voice, int staff, bool chord)
{
    std::string out = "<note>";
    if (chord)
        out += "<chord/>";
    out += pitch_xml(midi);
    out += "<duration>" + std::to_string(duration) + "</duration>";
    out += "<voice>" + std::to_string(voice) + "</voice>";
    out += "<type>";
    out += type;
    out += "</type><staff>" + std::to_string(staff) + "</staff></note>";
    return out;
}

std::vector<int> parse_chord_key(const std::string& key)
{
    std::vector<int> pitches;
    size_t at = 0;
    while (at < key.size())
    {
        const size_t plus = key.find('+', at);
        const std::string token = key.substr(at, plus == std::string::npos ? plus : plus - at);
        if (!token.empty())
            pitches.push_back(std::stoi(token));
        if (plus == std::string::npos)
            break;
        at = plus + 1;
    }
    std::sort(pitches.begin(), pitches.end());
    return pitches;
}

std::string chord_drill_measure_xml(const ChordDrillSpec& spec)
{
    if (spec.pitches.empty())
        return {};
    const int divisions = spec.divisions;
    const int beats = std::max(1, spec.beats);
    const int bar_duration = divisions * beats;

    const int bass = spec.pitches.front();
    std::vector<int> uppers(spec.pitches.begin() + 1, spec.pitches.end());
    if (uppers.empty())
        uppers.push_back(bass); // single-note drill: repeat it in both hands

    std::string out = "<measure number=\"1\">";
    if (spec.broken && divisions % 2 == 0 && beats >= 2)
    {
        // Broken form: arpeggiate the grab as eighths, land the block chord
        // on the final beat — the easier rung of the ladder.
        const int eighth = divisions / 2;
        const int broken_beats = beats - 1;
        for (int at = 0; at < broken_beats * 2; ++at)
            out += note_xml(
                uppers[static_cast<size_t>(at) % uppers.size()], eighth, "eighth", 1, 1);
        for (size_t at = 0; at < uppers.size(); ++at)
            out += note_xml(uppers[at], divisions, "quarter", 1, 1, at > 0);
    }
    else
    {
        // Block form: the grab repeated on every beat.
        for (int beat = 0; beat < beats; ++beat)
        {
            for (size_t at = 0; at < uppers.size(); ++at)
                out += note_xml(uppers[at], divisions, "quarter", 1, 1, at > 0);
        }
    }
    out += "<backup><duration>" + std::to_string(bar_duration) + "</duration></backup>";
    out += "<note>";
    out += pitch_xml(bass);
    out += "<duration>" + std::to_string(bar_duration) + "</duration><voice>5</voice>";
    if (beats == 3)
        out += "<type>half</type><dot/>";
    else if (beats == 4)
        out += "<type>whole</type>";
    else if (beats == 2)
        out += "<type>half</type>";
    else
        out += "<type>quarter</type>";
    out += "<staff>2</staff></note></measure>";
    return out;
}

std::string scale_measure_xml(const ScaleBarSpec& spec)
{
    const int divisions = spec.divisions;
    const int beats = std::max(1, spec.beats);

    static const int kMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int kMinor[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* degrees = spec.minor ? kMinor : kMajor;

    // Start on the tonic nearest below the troubled register's center.
    int start = spec.tonic_pc;
    while (start + 12 <= spec.center_pitch - 6)
        start += 12;

    const bool eighths = divisions % 2 == 0 && beats >= 2;
    const int count = eighths ? beats * 2 : beats;
    const int duration = eighths ? divisions / 2 : divisions;
    const char* type = eighths ? "eighth" : "quarter";

    std::string out = "<measure number=\"1\">";
    for (int at = 0; at < count; ++at)
    {
        const int degree = at % 7;
        const int octave_up = (at / 7) * 12;
        out += note_xml(start + degrees[degree] + octave_up, duration, type, 1, 1);
    }
    out += "</measure>";
    return out;
}

} // namespace scoreview
} // namespace draxul
