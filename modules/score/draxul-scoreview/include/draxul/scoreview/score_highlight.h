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

// Per-op lit flags over one ScoreDrawList, keyed by source element id — the
// note light-up overlay (plans/scoreview-conveyor.md). Deliberately an
// overlay rather than a draw-list mutation: buckets are built once per
// interpretation, flag flips are O(ops-per-note) on note events, and the
// renderer reads plain flag vectors with no per-frame string lookups.
struct ScoreHighlightState
{
    enum class OpKind : uint8_t
    {
        Glyph,
        Path,
        Text,
    };

    void build(const ScoreDrawList& list);
    void clear_lit();
    // Lights every op belonging to the element id (a note's notehead, stem,
    // accidental, ...). Returns false when the id has no ops.
    bool set_lit(const std::string& element_id);

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
