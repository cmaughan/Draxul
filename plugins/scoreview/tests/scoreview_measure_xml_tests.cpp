// The minimal MusicXML measure writer (plans/scoreview-stream.md S3): golden
// output for the fabricated drill/scale measures every composer shares, so
// tuplet/voice/duration regressions surface here instead of inside Verovio.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/measure_xml.h>

#include <string>
#include <vector>

using namespace draxul::scoreview;

TEST_CASE("chord keys parse back to sorted pitches", "[scoreview][measure-xml]")
{
    CHECK(parse_chord_key("60") == std::vector<int>{ 60 });
    CHECK(parse_chord_key("64+52+60") == std::vector<int>{ 52, 60, 64 });
    CHECK(parse_chord_key("") == std::vector<int>{});
}

TEST_CASE("pitch and note elements spell sharps and octaves", "[scoreview][measure-xml]")
{
    CHECK(pitch_xml(60) == "<pitch><step>C</step><octave>4</octave></pitch>");
    CHECK(pitch_xml(61) == "<pitch><step>C</step><alter>1</alter><octave>4</octave></pitch>");
    CHECK(note_xml(60, 2, "quarter", 1, 1)
        == "<note><pitch><step>C</step><octave>4</octave></pitch><duration>2</duration>"
           "<voice>1</voice><type>quarter</type><staff>1</staff></note>");
    CHECK(note_xml(64, 2, "quarter", 1, 1, /*chord=*/true).substr(0, 14) == "<note><chord/>");
}

TEST_CASE("the block drill strikes the grab on every beat over a held bass",
    "[scoreview][measure-xml]")
{
    ChordDrillSpec spec;
    spec.pitches = { 52, 60 };
    spec.divisions = 2;
    spec.beats = 3;
    spec.broken = false;
    const std::string xml = chord_drill_measure_xml(spec);
    const std::string upper = "<note><pitch><step>C</step><octave>4</octave></pitch>"
                              "<duration>2</duration><voice>1</voice><type>quarter</type>"
                              "<staff>1</staff></note>";
    CHECK(xml
        == "<measure number=\"1\">" + upper + upper + upper
            + "<backup><duration>6</duration></backup>"
              "<note><pitch><step>E</step><octave>3</octave></pitch>"
              "<duration>6</duration><voice>5</voice><type>half</type><dot/>"
              "<staff>2</staff></note></measure>");
}

TEST_CASE("the broken drill arpeggiates eighths then lands the block",
    "[scoreview][measure-xml]")
{
    ChordDrillSpec spec;
    spec.pitches = { 52, 60, 64 };
    spec.divisions = 2;
    spec.beats = 3;
    spec.broken = true;
    const std::string xml = chord_drill_measure_xml(spec);
    // Four eighths cycling the uppers (E4 C4 E4 C4 order: uppers are 60, 64).
    CHECK(xml.find("<type>eighth</type>") != std::string::npos);
    // The block lands as a quarter with a <chord/> member.
    CHECK(xml.find("<chord/>") != std::string::npos);
    // The bass holds the bar: dotted half in 3/4.
    CHECK(xml.find("<type>half</type><dot/>") != std::string::npos);
    // Whole-bar backup before the bass voice.
    CHECK(xml.find("<backup><duration>6</duration></backup>") != std::string::npos);
}

TEST_CASE("bass note types follow the meter", "[scoreview][measure-xml]")
{
    ChordDrillSpec spec;
    spec.pitches = { 48 };
    spec.divisions = 4;
    spec.broken = false;
    spec.beats = 4;
    CHECK(chord_drill_measure_xml(spec).find("<type>whole</type>") != std::string::npos);
    spec.beats = 2;
    CHECK(chord_drill_measure_xml(spec).find("<type>half</type><staff>2")
        != std::string::npos);
    spec.beats = 1;
    CHECK(chord_drill_measure_xml(spec).find("<type>quarter</type><staff>2")
        != std::string::npos);
    // A single-note drill repeats the note in the right hand too.
    CHECK(chord_drill_measure_xml(spec).find("<voice>1</voice>") != std::string::npos);
    // No pitches, no measure.
    spec.pitches.clear();
    CHECK(chord_drill_measure_xml(spec).empty());
}

TEST_CASE("the scale bar runs the key through the register", "[scoreview][measure-xml]")
{
    ScaleBarSpec spec;
    spec.tonic_pc = 9; // A
    spec.minor = true;
    spec.center_pitch = 66;
    spec.divisions = 2;
    spec.beats = 3;
    const std::string xml = scale_measure_xml(spec);
    // Six eighths in 3/4, ascending A natural minor from A3 (57):
    // A B C D E F -> 57 59 60 62 64 65.
    CHECK(xml
        == "<measure number=\"1\">" + note_xml(57, 1, "eighth", 1, 1)
            + note_xml(59, 1, "eighth", 1, 1) + note_xml(60, 1, "eighth", 1, 1)
            + note_xml(62, 1, "eighth", 1, 1) + note_xml(64, 1, "eighth", 1, 1)
            + note_xml(65, 1, "eighth", 1, 1) + "</measure>");
}

TEST_CASE("odd divisions fall back to quarter-note scale steps",
    "[scoreview][measure-xml]")
{
    ScaleBarSpec spec;
    spec.tonic_pc = 0;
    spec.minor = false;
    spec.center_pitch = 60;
    spec.divisions = 3; // eighths would not divide evenly
    spec.beats = 4;
    const std::string xml = scale_measure_xml(spec);
    CHECK(xml.find("<type>eighth</type>") == std::string::npos);
    // Four quarters: C D E F from the tonic below the register (C3 = 48).
    CHECK(xml
        == "<measure number=\"1\">" + note_xml(48, 3, "quarter", 1, 1)
            + note_xml(50, 3, "quarter", 1, 1) + note_xml(52, 3, "quarter", 1, 1)
            + note_xml(53, 3, "quarter", 1, 1) + "</measure>");
}
