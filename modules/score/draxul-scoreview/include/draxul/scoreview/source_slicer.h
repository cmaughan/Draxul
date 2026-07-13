#pragma once

// Source slicing for the rolling window (plans/scoreview-stream.md S2):
// cuts a MusicXML score into per-measure pieces and re-emits any bar range
// as a self-contained document — the original measures VERBATIM (the
// fidelity trick the recording script proved), with the accumulated
// attribute state (divisions, key, time, staves, clefs) injected at the
// window head so a window starting at bar 20 still knows what bar 1
// declared. Fabricated bars join in S3 through the same window path.

#include <memory>
#include <string>

namespace draxul
{
namespace scoreview
{

class SourceSlicer
{
public:
    SourceSlicer();
    ~SourceSlicer();

    SourceSlicer(const SourceSlicer&) = delete;
    SourceSlicer& operator=(const SourceSlicer&) = delete;

    // Parses and indexes the source. False (with `error`) when the XML
    // does not parse or holds no measures.
    bool load(const std::string& musicxml, std::string& error);
    bool ready() const;

    int bar_count() const;
    // Bar start on the quarter-note stream axis, accumulated from the
    // notated time signatures (bar 0 starts at 0).
    double bar_start_q(int bar) const;
    double bar_quarters(int bar) const;
    // The bar containing a stream position (clamped to valid bars).
    int bar_at(double stream_q) const;

    // A complete MusicXML document containing bars [first, first+count),
    // measures renumbered from 1, attribute state injected at the head.
    // Empty string when out of range.
    std::string window_xml(int first_bar, int count) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace scoreview
} // namespace draxul
