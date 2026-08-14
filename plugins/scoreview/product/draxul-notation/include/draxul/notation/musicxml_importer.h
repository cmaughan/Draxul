#pragma once

#include <draxul/notation/score_document.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{
namespace notation
{

struct MusicXmlImportResult
{
    ScoreDocument document;
    // Non-fatal oddities (overfull measures, unknown accidentals, ...). An
    // empty list means the file imported cleanly.
    std::vector<std::string> warnings;
};

// Parses an uncompressed MusicXML score-partwise document. Timeline elements
// (attributes, note, backup, forward) are imported; presentation-only elements
// (direction, print, barline, ...) are skipped by design in this phase.
// Returns std::nullopt and fills `error` on malformed XML, a non-partwise
// root, or compressed input.
std::optional<MusicXmlImportResult> import_musicxml(std::string_view xml_text, std::string& error);

// File convenience wrapper. Rejects compressed .mxl containers with a clear
// error (the Verovio layout phase consumes .mxl directly; this importer wants
// the inner uncompressed XML).
std::optional<MusicXmlImportResult> import_musicxml_file(const std::string& path, std::string& error);

} // namespace notation
} // namespace draxul
