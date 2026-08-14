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
    glyph_guide.assign(list.glyphs.size(), 0);
    path_guide.assign(list.paths.size(), 0);
    text_guide.assign(list.texts.size(), 0);
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

bool ScoreHighlightState::set_state(const std::string& element_id, State state)
{
    const auto found = buckets_.find(element_id);
    if (found == buckets_.end())
        return false;
    const uint8_t value = static_cast<uint8_t>(state);
    for (const auto& [kind, index] : found->second)
    {
        switch (kind)
        {
        case OpKind::Glyph:
            glyph_lit[static_cast<size_t>(index)] = value;
            break;
        case OpKind::Path:
            path_lit[static_cast<size_t>(index)] = value;
            break;
        case OpKind::Text:
            text_lit[static_cast<size_t>(index)] = value;
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

void ScoreHighlightState::clear_guidance()
{
    std::fill(glyph_guide.begin(), glyph_guide.end(), 0);
    std::fill(path_guide.begin(), path_guide.end(), 0);
    std::fill(text_guide.begin(), text_guide.end(), 0);
}

bool ScoreHighlightState::set_guidance(const std::string& element_id, int palette_index)
{
    const auto* ops = ops_for(element_id);
    if (ops == nullptr)
        return false;
    const uint8_t value = static_cast<uint8_t>(palette_index + 1);
    for (const auto& [kind, index] : *ops)
    {
        switch (kind)
        {
        case OpKind::Glyph:
            if (static_cast<size_t>(index) < glyph_guide.size())
                glyph_guide[static_cast<size_t>(index)] = value;
            break;
        case OpKind::Path:
            if (static_cast<size_t>(index) < path_guide.size())
                path_guide[static_cast<size_t>(index)] = value;
            break;
        case OpKind::Text:
            if (static_cast<size_t>(index) < text_guide.size())
                text_guide[static_cast<size_t>(index)] = value;
            break;
        }
    }
    return true;
}

} // namespace scoreview
} // namespace draxul
