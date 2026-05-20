#pragma once

#include <draxul/base_renderer.h>
#include <draxul/markdown/markdown_draw_list.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace draxul::markdown
{

class MarkdownRenderPass final : public draxul::IRenderPass
{
public:
    MarkdownRenderPass();
    ~MarkdownRenderPass() override;

    MarkdownRenderPass(const MarkdownRenderPass&) = delete;
    MarkdownRenderPass& operator=(const MarkdownRenderPass&) = delete;
    MarkdownRenderPass(MarkdownRenderPass&&) noexcept;
    MarkdownRenderPass& operator=(MarkdownRenderPass&&) noexcept;

    void set_draw_list(MarkdownDrawList draw_list, std::vector<MarkdownAtlasUpload> atlas_uploads, uint64_t draw_revision);

    void record_prepass(draxul::IRenderContext& ctx) override;
    void record(draxul::IRenderContext& ctx) override;

private:
    struct State;
    std::unique_ptr<State> state_;
    MarkdownDrawList draw_list_;
    std::vector<MarkdownAtlasUpload> atlas_uploads_;
    uint64_t draw_revision_ = 0;
};

std::unique_ptr<MarkdownRenderPass> create_markdown_render_pass();

} // namespace draxul::markdown
