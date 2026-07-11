#include <draxul/scoreview/score_highlight.h>

#include <algorithm>

namespace draxul
{
namespace scoreview
{

void ScoreHighlightState::build(const ScoreDrawList& list)
{
    buckets_.clear();
    glyph_lit.assign(list.glyphs.size(), 0);
    path_lit.assign(list.paths.size(), 0);
    text_lit.assign(list.texts.size(), 0);

    const auto add = [this](const std::string& id, OpKind kind, int index) {
        if (!id.empty())
            buckets_[id].emplace_back(kind, index);
    };
    for (size_t i = 0; i < list.glyphs.size(); ++i)
        add(list.glyphs[i].element_id, OpKind::Glyph, static_cast<int>(i));
    for (size_t i = 0; i < list.paths.size(); ++i)
        add(list.paths[i].element_id, OpKind::Path, static_cast<int>(i));
    for (size_t i = 0; i < list.texts.size(); ++i)
        add(list.texts[i].element_id, OpKind::Text, static_cast<int>(i));
}

void ScoreHighlightState::clear_lit()
{
    std::fill(glyph_lit.begin(), glyph_lit.end(), 0);
    std::fill(path_lit.begin(), path_lit.end(), 0);
    std::fill(text_lit.begin(), text_lit.end(), 0);
}

bool ScoreHighlightState::set_lit(const std::string& element_id)
{
    const auto found = buckets_.find(element_id);
    if (found == buckets_.end())
        return false;
    for (const auto& [kind, index] : found->second)
    {
        switch (kind)
        {
        case OpKind::Glyph:
            glyph_lit[static_cast<size_t>(index)] = 1;
            break;
        case OpKind::Path:
            path_lit[static_cast<size_t>(index)] = 1;
            break;
        case OpKind::Text:
            text_lit[static_cast<size_t>(index)] = 1;
            break;
        }
    }
    return true;
}

const std::vector<std::pair<ScoreHighlightState::OpKind, int>>* ScoreHighlightState::ops_for(
    const std::string& element_id) const
{
    const auto found = buckets_.find(element_id);
    return found == buckets_.end() ? nullptr : &found->second;
}

} // namespace scoreview
} // namespace draxul
