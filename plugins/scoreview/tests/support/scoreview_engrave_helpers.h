#pragma once

// Shared scoreview engrave-test helpers (kanban 22): the fixture reader and the
// "engrave this MusicXML and return its onsets" pass that the composer, stream,
// and window suites all reach for. One copy each, instead of the verbatim
// duplicates that had drifted across the test files. Every composer-#2 behavior
// test wants exactly these.

#include <draxul/scoreview/score_timemap.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

// Reads a repo-relative fixture (binary) into a string; empty on failure.
inline std::string read_fixture(const std::string& relative_path)
{
    std::ifstream stream(std::string(DRAXUL_PROJECT_ROOT) + relative_path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// The uncompressed Grieg waltz — the single-part grand-staff piece the composer
// and stream suites drive.
inline std::string read_grieg_xml()
{
    return read_fixture("/plugins/scoreview/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.musicxml");
}

// qstamp -> sorted sounding MIDI pitches, via a Verovio load of `xml`.
inline std::map<double, std::vector<int>> engrave_onsets(const std::string& xml)
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(std::string(DRAXUL_VEROVIO_DATA_DIR), error);
    REQUIRE(engine != nullptr);
    LayoutOptions options;
    options.mode = LayoutMode::Flow;
    engine->set_options(options);
    REQUIRE(engine->load(xml, error));
    auto timemap = parse_timemap(engine->render_timemap(), error);
    REQUIRE(timemap.has_value());
    std::map<double, std::vector<int>> onsets;
    for (const TimemapEntry& entry : timemap->entries)
    {
        for (const std::string& id : entry.note_on)
        {
            const int pitch = engine->midi_pitch_for_element(id);
            if (pitch >= 0)
                onsets[entry.qstamp].push_back(pitch);
        }
    }
    for (auto& [q, pitches] : onsets)
        std::sort(pitches.begin(), pitches.end());
    return onsets;
}

} // namespace scoreview
} // namespace draxul
