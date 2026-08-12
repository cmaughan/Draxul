#include <draxul/scoreview/score_host.h>

#include <draxul/base_renderer.h>
#include <draxul/host_registry.h>
#include <imgui.h>

namespace draxul::scoreview
{
namespace
{

class StaticFrameSink final : public ScoreFrameSink
{
public:
    explicit StaticFrameSink(IFrameContext& frame)
        : frame_(frame)
    {
    }

    void record_canvas(INanoVGPass& pass, int x, int y,
        int width, int height) override
    {
        frame_.record_render_pass(pass, { x, y, width, height });
    }
    void render_overlay(void* draw_data, void* context) override
    {
        frame_.render_imgui(static_cast<ImDrawData*>(draw_data),
            static_cast<ImGuiContext*>(context));
    }
    void finish() override { frame_.flush_submit_chunk(); }

private:
    IFrameContext& frame_;
};

} // namespace

void ScoreHost::draw(IFrameContext& frame)
{
    StaticFrameSink sink(frame);
    ScoreRuntime::draw(sink);
}

std::unique_ptr<IHost> create_score_host()
{
    return std::make_unique<ScoreHost>();
}

void register_score_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::Score, create_score_host);
}

} // namespace draxul::scoreview
