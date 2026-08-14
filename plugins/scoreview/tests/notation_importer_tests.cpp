#include <catch2/catch_all.hpp>

#include <draxul/notation/musicxml_importer.h>

#include <set>
#include <string>

using namespace draxul::notation;

namespace
{

std::string wrap_score(const std::string& measures)
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <work><work-title>Test Piece</work-title></work>
  <identification><creator type="composer"> Composer Name </creator></identification>
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">)"
        + measures + R"(</part>
</score-partwise>)";
}

MusicXmlImportResult import_ok(const std::string& xml)
{
    std::string error;
    auto result = import_musicxml(xml, error);
    INFO(error);
    REQUIRE(result.has_value());
    return std::move(*result);
}

std::string grieg_fixture_path(const char* extension)
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/plugins/scoreview/tests/fixtures/musicxml/grieg-waltz-op-12-no-2." + extension;
}

} // namespace

TEST_CASE("Fraction normalizes and does arithmetic", "[notation]")
{
    CHECK(Fraction::of(2, 8) == Fraction::of(1, 4));
    CHECK(Fraction::of(-2, -8) == Fraction::of(1, 4));
    CHECK(Fraction::of(2, -8) == Fraction::of(-1, 4));
    CHECK(Fraction::of(0, 7) == Fraction{});
    CHECK((Fraction::of(1, 4) + Fraction::of(1, 6)) == Fraction::of(5, 12));
    CHECK((Fraction::of(3, 4) - Fraction::of(3, 4)).is_zero());
    CHECK((Fraction::of(1, 4) - Fraction::of(1, 2)).is_negative());
    CHECK(Fraction::of(1, 3) < Fraction::of(1, 2));
    CHECK(Fraction::of(2, 4) <= Fraction::of(1, 2));
    CHECK(Fraction::of(3, 4) > Fraction::of(2, 3));
    CHECK(Fraction::of(1, 2).to_string() == "1/2");
    CHECK(TimeSignature{ 3, 4 }.measure_duration() == Fraction::of(3, 4));
}

TEST_CASE("imports a single note with header metadata", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes>
          <divisions>1</divisions>
          <key><fifths>2</fifths></key>
          <time><beats>4</beats><beat-type>4</beat-type></time>
          <clef><sign>G</sign><line>2</line></clef>
        </attributes>
        <note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice></note>
      </measure>)"));

    CHECK(result.warnings.empty());
    CHECK(result.document.title == "Test Piece");
    CHECK(result.document.composer == "Composer Name");
    REQUIRE(result.document.parts.size() == 1);

    const Part& part = result.document.parts[0];
    CHECK(part.id == "P1");
    CHECK(part.name == "Piano");
    CHECK(part.staves == 1);
    REQUIRE(part.measures.size() == 1);

    const Measure& measure = part.measures[0];
    CHECK(measure.number == "1");
    CHECK_FALSE(measure.implicit);
    REQUIRE(measure.key.has_value());
    CHECK(measure.key->fifths == 2);
    REQUIRE(measure.time.has_value());
    CHECK(measure.time->beats == 4);
    CHECK(measure.time->beat_type == 4);
    REQUIRE(measure.clefs.size() == 1);
    CHECK(measure.clefs[0].sign == 'G');
    CHECK(measure.clefs[0].line == 2);

    REQUIRE(measure.notes.size() == 1);
    const Note& note = measure.notes[0];
    CHECK(note.id != 0);
    CHECK_FALSE(note.is_rest);
    CHECK(note.pitch.step == Step::C);
    CHECK(note.pitch.octave == 4);
    CHECK(note.pitch.alter == 0);
    CHECK(note.onset == Fraction{});
    CHECK(note.duration == Fraction::of(1, 1));
    CHECK(note.voice == 1);
    CHECK(note.staff == 1);
}

TEST_CASE("divisions changes mid-file keep durations exact", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes><divisions>2</divisions><time><beats>1</beats><beat-type>4</beat-type></time></attributes>
        <note><pitch><step>C</step><octave>4</octave></pitch><duration>2</duration></note>
      </measure>
      <measure number="2">
        <attributes><divisions>480</divisions></attributes>
        <note><pitch><step>D</step><octave>4</octave></pitch><duration>480</duration></note>
      </measure>)"));

    CHECK(result.warnings.empty());
    const Part& part = result.document.parts[0];
    REQUIRE(part.measures.size() == 2);
    CHECK(part.measures[0].notes[0].duration == Fraction::of(1, 4));
    CHECK(part.measures[1].notes[0].duration == Fraction::of(1, 4));
}

TEST_CASE("chord members share the onset of their first note", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes><divisions>1</divisions><time><beats>2</beats><beat-type>4</beat-type></time></attributes>
        <note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration></note>
        <note><chord/><pitch><step>E</step><octave>4</octave></pitch><duration>1</duration></note>
        <note><chord/><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration></note>
        <note><pitch><step>D</step><octave>4</octave></pitch><duration>1</duration></note>
      </measure>)"));

    CHECK(result.warnings.empty());
    const Measure& measure = result.document.parts[0].measures[0];
    REQUIRE(measure.notes.size() == 4);
    CHECK_FALSE(measure.notes[0].chord_with_prev);
    CHECK(measure.notes[1].chord_with_prev);
    CHECK(measure.notes[2].chord_with_prev);
    CHECK(measure.notes[0].onset == Fraction{});
    CHECK(measure.notes[1].onset == Fraction{});
    CHECK(measure.notes[2].onset == Fraction{});
    CHECK(measure.notes[3].onset == Fraction::of(1, 4));
}

TEST_CASE("backup interleaves voices across two staves", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes>
          <divisions>2</divisions>
          <time><beats>2</beats><beat-type>4</beat-type></time>
          <staves>2</staves>
          <clef number="1"><sign>G</sign><line>2</line></clef>
          <clef number="2"><sign>F</sign><line>4</line></clef>
        </attributes>
        <note><pitch><step>E</step><octave>5</octave></pitch><duration>2</duration><voice>1</voice><staff>1</staff></note>
        <note><pitch><step>F</step><octave>5</octave></pitch><duration>2</duration><voice>1</voice><staff>1</staff></note>
        <backup><duration>4</duration></backup>
        <note><pitch><step>C</step><octave>3</octave></pitch><duration>4</duration><voice>5</voice><staff>2</staff></note>
      </measure>)"));

    CHECK(result.warnings.empty());
    const Part& part = result.document.parts[0];
    CHECK(part.staves == 2);
    const Measure& measure = part.measures[0];
    REQUIRE(measure.clefs.size() == 2);
    CHECK(measure.clefs[1].staff == 2);
    CHECK(measure.clefs[1].sign == 'F');
    CHECK(measure.clefs[1].line == 4);
    REQUIRE(measure.notes.size() == 3);
    CHECK(measure.notes[0].onset == Fraction{});
    CHECK(measure.notes[1].onset == Fraction::of(1, 4));
    CHECK(measure.notes[2].onset == Fraction{});
    CHECK(measure.notes[2].voice == 5);
    CHECK(measure.notes[2].staff == 2);
    CHECK(measure.notes[2].duration == Fraction::of(1, 2));
}

TEST_CASE("dotted and tuplet durations stay exact", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes><divisions>6</divisions><time><beats>3</beats><beat-type>4</beat-type></time></attributes>
        <note><pitch><step>C</step><octave>4</octave></pitch><duration>9</duration><dot/></note>
        <note><pitch><step>D</step><octave>4</octave></pitch><duration>3</duration></note>
        <note><pitch><step>E</step><octave>4</octave></pitch><duration>2</duration>
          <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification></note>
        <note><pitch><step>F</step><octave>4</octave></pitch><duration>2</duration>
          <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification></note>
        <note><pitch><step>G</step><octave>4</octave></pitch><duration>2</duration>
          <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification></note>
      </measure>)"));

    CHECK(result.warnings.empty());
    const Measure& measure = result.document.parts[0].measures[0];
    REQUIRE(measure.notes.size() == 5);
    CHECK(measure.notes[0].duration == Fraction::of(3, 8)); // dotted quarter
    CHECK(measure.notes[0].dots == 1);
    CHECK(measure.notes[2].duration == Fraction::of(1, 12)); // triplet eighth
    REQUIRE(measure.notes[2].tuplet.has_value());
    CHECK(measure.notes[2].tuplet->actual_notes == 3);
    CHECK(measure.notes[2].tuplet->normal_notes == 2);
    CHECK(measure.notes[4].onset == Fraction::of(3, 4) - Fraction::of(1, 12));
}

TEST_CASE("grace notes carry no duration and do not advance the cursor", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes><divisions>1</divisions><time><beats>1</beats><beat-type>4</beat-type></time></attributes>
        <note><grace/><pitch><step>B</step><octave>4</octave></pitch><voice>1</voice></note>
        <note><pitch><step>C</step><octave>5</octave></pitch><duration>1</duration><voice>1</voice></note>
      </measure>)"));

    CHECK(result.warnings.empty());
    const Measure& measure = result.document.parts[0].measures[0];
    REQUIRE(measure.notes.size() == 2);
    CHECK(measure.notes[0].grace);
    CHECK(measure.notes[0].duration == Fraction{});
    CHECK(measure.notes[0].onset == Fraction{});
    CHECK(measure.notes[1].onset == Fraction{});
    CHECK(measure.notes[1].duration == Fraction::of(1, 4));
}

TEST_CASE("ties, accidentals, and alter are captured", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes><divisions>1</divisions><time><beats>3</beats><beat-type>4</beat-type></time></attributes>
        <note><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>1</duration>
          <tie type="start"/><accidental>sharp</accidental></note>
        <note><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>1</duration>
          <tie type="stop"/><tie type="start"/></note>
        <note><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>1</duration>
          <tie type="stop"/></note>
      </measure>)"));

    CHECK(result.warnings.empty());
    const Measure& measure = result.document.parts[0].measures[0];
    REQUIRE(measure.notes.size() == 3);
    CHECK(measure.notes[0].tie == TieState::Start);
    CHECK(measure.notes[1].tie == TieState::Both);
    CHECK(measure.notes[2].tie == TieState::Stop);
    CHECK(measure.notes[0].pitch.alter == 1);
    REQUIRE(measure.notes[0].written_accidental.has_value());
    CHECK(*measure.notes[0].written_accidental == Accidental::Sharp);
    CHECK_FALSE(measure.notes[1].written_accidental.has_value());
}

TEST_CASE("overfull and underfull measures warn but import", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="1">
        <attributes><divisions>1</divisions><time><beats>1</beats><beat-type>4</beat-type></time></attributes>
        <note><pitch><step>C</step><octave>4</octave></pitch><duration>2</duration></note>
      </measure>)"));
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0].find("measure 1") != std::string::npos);
    CHECK(result.warnings[0].find("1/2") != std::string::npos);
    CHECK(result.warnings[0].find("1/4") != std::string::npos);
    CHECK(result.document.parts[0].measures[0].notes.size() == 1);
}

TEST_CASE("implicit pickup measures skip fill validation", "[notation]")
{
    auto result = import_ok(wrap_score(R"(
      <measure number="0" implicit="yes">
        <attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time></attributes>
        <note><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration></note>
      </measure>)"));
    CHECK(result.warnings.empty());
    CHECK(result.document.parts[0].measures[0].implicit);
}

TEST_CASE("rejects malformed, timewise, and compressed input", "[notation]")
{
    std::string error;
    CHECK_FALSE(import_musicxml("this is not xml <", error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(import_musicxml("<score-timewise version=\"3.1\"></score-timewise>", error)
            .has_value());
    CHECK(error.find("timewise") != std::string::npos);

    error.clear();
    CHECK_FALSE(import_musicxml("<opus></opus>", error).has_value());
    CHECK(error.find("opus") != std::string::npos);

    error.clear();
    CHECK_FALSE(import_musicxml(std::string("PK\x03\x04") + "not really a zip", error).has_value());
    CHECK(error.find(".mxl") != std::string::npos);

    error.clear();
    CHECK_FALSE(import_musicxml_file(grieg_fixture_path("mxl"), error).has_value());
    CHECK(error.find(".mxl") != std::string::npos);

    error.clear();
    CHECK_FALSE(import_musicxml_file("/nonexistent/nope.musicxml", error).has_value());
    CHECK(error.find("could not open") != std::string::npos);
}

TEST_CASE("imports the Grieg waltz fixture faithfully", "[notation]")
{
    std::string error;
    auto result = import_musicxml_file(grieg_fixture_path("musicxml"), error);
    INFO(error);
    REQUIRE(result.has_value());
    for (const std::string& warning : result->warnings)
        UNSCOPED_INFO(warning);
    CHECK(result->warnings.empty());

    const ScoreDocument& doc = result->document;
    CHECK(doc.title == "Walz"); // sic — the fixture's movement-title
    CHECK(doc.composer == "Grieg");
    REQUIRE(doc.parts.size() == 1);

    const Part& part = doc.parts[0];
    CHECK(part.id == "P1");
    CHECK(part.name == "Grand Piano");
    CHECK(part.staves == 2);
    REQUIRE(part.measures.size() == 79);

    // Ground truth counted independently from the XML (see plans/scoreview.md).
    size_t notes = 0;
    size_t rests = 0;
    size_t grace_notes = 0;
    size_t chord_members = 0;
    size_t tuplet_notes = 0;
    size_t accidental_notes = 0;
    size_t clef_changes = 0;
    size_t key_measures = 0;
    size_t time_measures = 0;
    int dot_count = 0;
    int tie_elements = 0;
    std::set<int> voices;
    std::set<uint64_t> ids;
    const Fraction measure_span = Fraction::of(3, 4);
    bool onsets_in_range = true;

    for (const Measure& measure : part.measures)
    {
        clef_changes += measure.clefs.size();
        key_measures += measure.key.has_value() ? 1 : 0;
        time_measures += measure.time.has_value() ? 1 : 0;
        for (const Note& note : measure.notes)
        {
            ++notes;
            rests += note.is_rest ? 1 : 0;
            grace_notes += note.grace ? 1 : 0;
            chord_members += note.chord_with_prev ? 1 : 0;
            tuplet_notes += note.tuplet.has_value() ? 1 : 0;
            accidental_notes += note.written_accidental.has_value() ? 1 : 0;
            dot_count += note.dots;
            if (note.tie == TieState::Both)
                tie_elements += 2;
            else if (note.tie != TieState::None)
                tie_elements += 1;
            voices.insert(note.voice);
            ids.insert(note.id);
            if (measure_span < note.onset + note.duration)
                onsets_in_range = false;
        }
    }

    CHECK(notes == 694);
    CHECK(rests == 49);
    CHECK(grace_notes == 14);
    CHECK(chord_members == 202);
    CHECK(tuplet_notes == 36);
    CHECK(accidental_notes == 49);
    CHECK(dot_count == 65);
    CHECK(tie_elements == 36);
    CHECK(clef_changes == 4);
    CHECK(key_measures == 4);
    CHECK(time_measures == 1);
    CHECK(voices == std::set<int>{ 1, 2, 5 });
    CHECK(ids.size() == notes); // every note id unique
    CHECK(onsets_in_range); // no note extends past its 3/4 measure
    REQUIRE(part.measures[0].time.has_value());
    CHECK(part.measures[0].time->beats == 3);
    CHECK(part.measures[0].time->beat_type == 4);
    REQUIRE(part.measures[0].clefs.size() == 2);
}
