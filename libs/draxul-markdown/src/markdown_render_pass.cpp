#include <draxul/markdown/markdown_render_pass.h>

#include <iterator>
#include <utility>

namespace draxul::markdown
{

void MarkdownRenderPass::set_draw_list(
    MarkdownDrawList draw_list,
    std::vector<MarkdownAtlasUpload> atlas_uploads,
    uint64_t draw_revision)
{
    draw_list_ = std::move(draw_list);
    atlas_uploads_.insert(
        atlas_uploads_.end(),
        std::make_move_iterator(atlas_uploads.begin()),
        std::make_move_iterator(atlas_uploads.end()));
    draw_revision_ = draw_revision;
}

std::unique_ptr<MarkdownRenderPass> create_markdown_render_pass()
{
    return std::make_unique<MarkdownRenderPass>();
}

} // namespace draxul::markdown
