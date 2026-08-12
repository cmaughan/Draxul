#include <draxul/satview/satview_host.h>

#include <draxul/host_registry.h>
#include <draxul/satview/satview_scene_pass.h>
#include <imgui.h>

namespace draxul::satview
{

namespace
{

class StaticFrameSink final : public SatViewFrameSink
{
public:
    explicit StaticFrameSink(IFrameContext& frame)
        : frame_(frame)
    {
    }

    void record_scene(SatViewScenePass& pass,
        int x, int y, int width, int height) override
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

void SatViewHost::draw(IFrameContext& frame)
{
    StaticFrameSink sink(frame);
    SatViewRuntime::draw(sink);
}

std::unique_ptr<IHost> create_satview_host()
{
    return std::make_unique<SatViewHost>();
}

void register_satview_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::SatView, create_satview_host);
}

} // namespace draxul::satview
