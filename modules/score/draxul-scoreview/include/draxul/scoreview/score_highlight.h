#pragma once

#include <draxul/scoreview/score_draw_list.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul
{
namespace scoreview
{

// Per-op highlight states over one ScoreDrawList, keyed by source element id
// (plans/scoreview-conveyor.md, extended by plans/scoreview-gate.md).
// Deliberately an overlay rather than a draw-list mutation: buckets are
// built once per interpretation, state flips are O(ops-per-note) on note
// events, and the renderer reads plain state vectors with no per-frame
// string lookups.
struct ScoreHighlightState
{
    enum class OpKind : uint8_t
    {
        Glyph,
        Path,
        Text,
    };

    // Per-op visual state; the renderer maps these to colors (ink, amber,
    // green, red respectively).
    enum class State : uint8_t
    {
        None = 0,
        Passed = 1, // clock-mode conveyor crossed the onset
        Correct = 2, // gate judged the note correct
        Missed = 3, // gate opened with this note unmatched
    };

    void build(const ScoreDrawList& list);
    void clear_lit();
    // Sets the state of every op belonging to the element id (a note's
    // notehead, stem, accidental, ...). Returns false when the id has no ops.
    bool set_state(const std::string& element_id, State state);
    // Clock-mode convenience: light as Passed.
    bool set_lit(const std::string& element_id)
    {
        return set_state(element_id, State::Passed);
    }

    bool empty() const
    {
        return buckets_.empty();
    }
    size_t bucket_count() const
    {
        return buckets_.size();
    }
    const std::vector<std::pair<OpKind, int>>* ops_for(const std::string& element_id) const;

    std::vector<uint8_t> glyph_lit;
    std::vector<uint8_t> path_lit;
    std::vector<uint8_t> text_lit;

private:
    std::unordered_map<std::string, std::vector<std::pair<OpKind, int>>> buckets_;
};

} // namespace scoreview
} // namespace draxul
