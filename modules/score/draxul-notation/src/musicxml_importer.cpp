#include <draxul/notation/musicxml_importer.h>

#include <tinyxml2.h>

#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace draxul
{
namespace notation
{

namespace
{

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

std::string trim(std::string_view s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return std::string(s.substr(begin, end - begin));
}

std::string text_of(const XMLElement* element)
{
    if (element == nullptr || element->GetText() == nullptr)
        return {};
    return element->GetText();
}

std::string child_text(const XMLElement* parent, const char* name)
{
    if (parent == nullptr)
        return {};
    return text_of(parent->FirstChildElement(name));
}

int child_int(const XMLElement* parent, const char* name, int fallback)
{
    const std::string text = child_text(parent, name);
    if (text.empty())
        return fallback;
    try
    {
        return std::stoi(text);
    }
    catch (...)
    {
        return fallback;
    }
}

std::optional<Accidental> parse_accidental(const std::string& text)
{
    if (text.empty())
        return std::nullopt;
    if (text == "sharp")
        return Accidental::Sharp;
    if (text == "flat")
        return Accidental::Flat;
    if (text == "natural")
        return Accidental::Natural;
    if (text == "double-sharp" || text == "sharp-sharp")
        return Accidental::DoubleSharp;
    if (text == "flat-flat")
        return Accidental::DoubleFlat;
    return Accidental::Other;
}

std::optional<Step> parse_step(const std::string& text)
{
    if (text.size() != 1)
        return std::nullopt;
    switch (text[0])
    {
    case 'C':
        return Step::C;
    case 'D':
        return Step::D;
    case 'E':
        return Step::E;
    case 'F':
        return Step::F;
    case 'G':
        return Step::G;
    case 'A':
        return Step::A;
    case 'B':
        return Step::B;
    default:
        return std::nullopt;
    }
}

class Importer
{
public:
    std::optional<MusicXmlImportResult> run(std::string_view xml, std::string& error)
    {
        XMLDocument xml_doc;
        if (xml_doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS)
        {
            error = std::string("MusicXML parse error: ") + xml_doc.ErrorStr();
            return std::nullopt;
        }

        const XMLElement* root = xml_doc.RootElement();
        if (root == nullptr)
        {
            error = "MusicXML parse error: document has no root element";
            return std::nullopt;
        }
        if (std::strcmp(root->Name(), "score-timewise") == 0)
        {
            error = "score-timewise MusicXML is not supported; re-export as score-partwise";
            return std::nullopt;
        }
        if (std::strcmp(root->Name(), "score-partwise") != 0)
        {
            error = std::string("unexpected MusicXML root element <") + root->Name() + ">";
            return std::nullopt;
        }

        parse_header(root);

        for (const XMLElement* part_el = root->FirstChildElement("part"); part_el != nullptr;
            part_el = part_el->NextSiblingElement("part"))
        {
            parse_part(part_el);
        }

        if (result_.document.parts.empty())
        {
            error = "MusicXML document contains no <part> elements";
            return std::nullopt;
        }
        return std::move(result_);
    }

private:
    void warn(std::string message)
    {
        std::string prefix;
        if (!current_part_id_.empty())
            prefix += "part " + current_part_id_;
        if (!current_measure_number_.empty())
            prefix += (prefix.empty() ? "measure " : ", measure ") + current_measure_number_;
        if (!prefix.empty())
            message = prefix + ": " + message;
        result_.warnings.push_back(std::move(message));
    }

    void parse_header(const XMLElement* root)
    {
        ScoreDocument& doc = result_.document;
        doc.title = trim(child_text(root->FirstChildElement("work"), "work-title"));
        if (doc.title.empty())
            doc.title = trim(child_text(root, "movement-title"));

        if (const XMLElement* identification = root->FirstChildElement("identification"))
        {
            for (const XMLElement* creator = identification->FirstChildElement("creator");
                creator != nullptr; creator = creator->NextSiblingElement("creator"))
            {
                const char* type = creator->Attribute("type");
                if (type != nullptr && std::strcmp(type, "composer") == 0)
                {
                    doc.composer = trim(text_of(creator));
                    break;
                }
            }
        }

        if (const XMLElement* part_list = root->FirstChildElement("part-list"))
        {
            for (const XMLElement* score_part = part_list->FirstChildElement("score-part");
                score_part != nullptr; score_part = score_part->NextSiblingElement("score-part"))
            {
                const char* id = score_part->Attribute("id");
                if (id != nullptr)
                    part_names_.emplace_back(id, trim(child_text(score_part, "part-name")));
            }
        }
    }

    void parse_part(const XMLElement* part_el)
    {
        Part part;
        part.id = part_el->Attribute("id") != nullptr ? part_el->Attribute("id") : "";
        for (const auto& [id, name] : part_names_)
        {
            if (id == part.id)
            {
                part.name = name;
                break;
            }
        }
        current_part_id_ = part.id;

        // Divisions and the active time signature persist across measures.
        divisions_ = 1;
        current_time_.reset();

        for (const XMLElement* measure_el = part_el->FirstChildElement("measure");
            measure_el != nullptr; measure_el = measure_el->NextSiblingElement("measure"))
        {
            part.measures.push_back(parse_measure(measure_el, part));
        }

        current_part_id_.clear();
        current_measure_number_.clear();
        result_.document.parts.push_back(std::move(part));
    }

    Measure parse_measure(const XMLElement* measure_el, Part& part)
    {
        Measure measure;
        measure.number = measure_el->Attribute("number") != nullptr
            ? measure_el->Attribute("number")
            : "";
        const char* implicit = measure_el->Attribute("implicit");
        measure.implicit = implicit != nullptr && std::strcmp(implicit, "yes") == 0;
        current_measure_number_ = measure.number;

        cursor_ = Fraction{};
        max_cursor_ = Fraction{};
        last_onset_ = Fraction{};

        // Timeline elements are processed in document order; everything else
        // (direction, print, barline, harmony, sound, ...) is presentation or
        // playback detail this phase deliberately skips.
        for (const XMLElement* child = measure_el->FirstChildElement(); child != nullptr;
            child = child->NextSiblingElement())
        {
            const char* name = child->Name();
            if (std::strcmp(name, "attributes") == 0)
                parse_attributes(child, part, measure);
            else if (std::strcmp(name, "note") == 0)
                parse_note(child, measure);
            else if (std::strcmp(name, "backup") == 0)
                apply_cursor_shift(child, /*forward=*/false);
            else if (std::strcmp(name, "forward") == 0)
                apply_cursor_shift(child, /*forward=*/true);
        }

        validate_fill(measure);
        return measure;
    }

    void parse_attributes(const XMLElement* attributes, Part& part, Measure& measure)
    {
        const int divisions = child_int(attributes, "divisions", 0);
        if (divisions > 0)
            divisions_ = divisions;
        else if (divisions < 0 || !child_text(attributes, "divisions").empty())
            warn("ignoring invalid <divisions> value '" + child_text(attributes, "divisions") + "'");

        if (const XMLElement* key = attributes->FirstChildElement("key"))
            measure.key = KeySignature{ child_int(key, "fifths", 0) };

        if (const XMLElement* time = attributes->FirstChildElement("time"))
        {
            TimeSignature sig;
            sig.beats = child_int(time, "beats", 4);
            sig.beat_type = child_int(time, "beat-type", 4);
            measure.time = sig;
            current_time_ = sig;
        }

        const int staves = child_int(attributes, "staves", 0);
        if (staves > 0)
            part.staves = std::max(part.staves, staves);

        for (const XMLElement* clef = attributes->FirstChildElement("clef"); clef != nullptr;
            clef = clef->NextSiblingElement("clef"))
        {
            ClefChange change;
            change.staff = clef->IntAttribute("number", 1);
            const std::string sign = child_text(clef, "sign");
            change.sign = sign.empty() ? 'G' : sign[0];
            change.line = child_int(clef, "line", change.sign == 'F' ? 4 : 2);
            change.octave_change = child_int(clef, "clef-octave-change", 0);
            change.onset = cursor_;
            measure.clefs.push_back(change);
        }
    }

    // Reads <duration> (in divisions) and converts to whole-note units.
    // Returns nullopt when the element is absent.
    std::optional<Fraction> read_duration(const XMLElement* parent)
    {
        const XMLElement* duration_el = parent->FirstChildElement("duration");
        if (duration_el == nullptr)
            return std::nullopt;
        const std::string text = trim(text_of(duration_el));
        double value = 0.0;
        try
        {
            value = std::stod(text);
        }
        catch (...)
        {
            warn("unparseable <duration> '" + text + "'");
            return std::nullopt;
        }
        const long long rounded = std::llround(value);
        if (std::abs(value - static_cast<double>(rounded)) > 1e-6)
            warn("non-integer <duration> '" + text + "' rounded to " + std::to_string(rounded));
        if (rounded < 0)
        {
            warn("negative <duration> '" + text + "' ignored");
            return std::nullopt;
        }
        // divisions counts per quarter note; 4 quarters per whole note.
        return Fraction::of(rounded, static_cast<long long>(divisions_) * 4);
    }

    void apply_cursor_shift(const XMLElement* element, bool forward)
    {
        const std::optional<Fraction> amount = read_duration(element);
        if (!amount)
        {
            warn(forward ? "<forward> without usable <duration>"
                         : "<backup> without usable <duration>");
            return;
        }
        if (forward)
        {
            cursor_ += *amount;
            if (max_cursor_ < cursor_)
                max_cursor_ = cursor_;
        }
        else
        {
            cursor_ -= *amount;
            if (cursor_.is_negative())
            {
                warn("<backup> moved before the start of the measure; clamping to 0");
                cursor_ = Fraction{};
            }
        }
    }

    void parse_note(const XMLElement* note_el, Measure& measure)
    {
        Note note;
        note.id = next_note_id_++;
        note.grace = note_el->FirstChildElement("grace") != nullptr;
        note.chord_with_prev = note_el->FirstChildElement("chord") != nullptr;
        note.is_rest = note_el->FirstChildElement("rest") != nullptr;
        note.voice = child_int(note_el, "voice", 1);
        note.staff = child_int(note_el, "staff", 1);

        if (!note.is_rest)
        {
            const XMLElement* pitch_el = note_el->FirstChildElement("pitch");
            if (pitch_el != nullptr)
            {
                const std::string step_text = child_text(pitch_el, "step");
                const std::optional<Step> step = parse_step(step_text);
                if (step)
                    note.pitch.step = *step;
                else
                    warn("invalid pitch <step> '" + step_text + "'; defaulting to C");
                note.pitch.octave = child_int(pitch_el, "octave", 4);

                const std::string alter_text = child_text(pitch_el, "alter");
                if (!alter_text.empty())
                {
                    const double alter = std::strtod(alter_text.c_str(), nullptr);
                    note.pitch.alter = static_cast<int>(std::llround(alter));
                    if (std::abs(alter - note.pitch.alter) > 1e-6)
                        warn("microtonal <alter> '" + alter_text + "' rounded to " + std::to_string(note.pitch.alter));
                }
            }
            else if (note_el->FirstChildElement("unpitched") != nullptr)
            {
                note.is_rest = true;
                warn("<unpitched> note treated as rest");
            }
            else
            {
                note.is_rest = true;
                warn("<note> without <pitch> or <rest> treated as rest");
            }
        }

        if (note.grace)
        {
            note.duration = Fraction{};
        }
        else
        {
            const std::optional<Fraction> duration = read_duration(note_el);
            if (duration)
            {
                note.duration = *duration;
            }
            else
            {
                warn("non-grace <note> without usable <duration>; treated as zero-length");
                note.duration = Fraction{};
            }
        }

        int dots = 0;
        for (const XMLElement* dot = note_el->FirstChildElement("dot"); dot != nullptr;
            dot = dot->NextSiblingElement("dot"))
            ++dots;
        note.dots = dots;

        bool tie_start = false;
        bool tie_stop = false;
        for (const XMLElement* tie = note_el->FirstChildElement("tie"); tie != nullptr;
            tie = tie->NextSiblingElement("tie"))
        {
            const char* type = tie->Attribute("type");
            if (type == nullptr)
                continue;
            if (std::strcmp(type, "start") == 0)
                tie_start = true;
            else if (std::strcmp(type, "stop") == 0)
                tie_stop = true;
        }
        if (tie_start && tie_stop)
            note.tie = TieState::Both;
        else if (tie_start)
            note.tie = TieState::Start;
        else if (tie_stop)
            note.tie = TieState::Stop;

        note.written_accidental = parse_accidental(child_text(note_el, "accidental"));

        if (const XMLElement* time_mod = note_el->FirstChildElement("time-modification"))
        {
            TimeModification mod;
            mod.actual_notes = child_int(time_mod, "actual-notes", 1);
            mod.normal_notes = child_int(time_mod, "normal-notes", 1);
            note.tuplet = mod;
        }

        // Onset bookkeeping: chord members share the previous non-chord onset;
        // grace notes sit at the cursor without advancing it.
        if (note.chord_with_prev)
        {
            note.onset = last_onset_;
        }
        else
        {
            note.onset = cursor_;
            last_onset_ = cursor_;
            if (!note.grace)
            {
                cursor_ += note.duration;
                if (max_cursor_ < cursor_)
                    max_cursor_ = cursor_;
            }
        }

        measure.notes.push_back(note);
    }

    void validate_fill(const Measure& measure)
    {
        if (!current_time_ || measure.implicit)
            return;
        const Fraction expected = current_time_->measure_duration();
        if (max_cursor_ == expected)
            return;
        if (measure.notes.empty())
            return; // attribute/layout-only measures are legal
        warn("measure content spans " + max_cursor_.to_string() + " of an expected " + expected.to_string());
    }

    MusicXmlImportResult result_;
    std::vector<std::pair<std::string, std::string>> part_names_;
    std::string current_part_id_;
    std::string current_measure_number_;
    int divisions_ = 1;
    std::optional<TimeSignature> current_time_;
    Fraction cursor_;
    Fraction max_cursor_;
    Fraction last_onset_;
    uint64_t next_note_id_ = 1;
};

} // namespace

std::optional<MusicXmlImportResult> import_musicxml(std::string_view xml_text, std::string& error)
{
    if (xml_text.size() >= 4 && xml_text.compare(0, 4, "PK\x03\x04") == 0)
    {
        error = "input is a compressed .mxl container; unzip it to .musicxml first "
                "(the Verovio layout phase consumes .mxl directly)";
        return std::nullopt;
    }
    Importer importer;
    return importer.run(xml_text, error);
}

std::optional<MusicXmlImportResult> import_musicxml_file(const std::string& path, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "could not open '" + path + "'";
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return import_musicxml(buffer.str(), error);
}

} // namespace notation
} // namespace draxul
