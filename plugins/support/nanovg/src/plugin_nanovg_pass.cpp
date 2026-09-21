#include <draxul/plugin_nanovg_pass.h>

#include "plugin_nanovg_pass_internal.h"

#include "nanovg.h"

#include <algorithm>
#include <utility>

namespace draxul::plugin_support
{
namespace
{

class NativeFrameOperations final : public detail::PluginNanoVGFrameOperations
{
public:
    void begin_frame(NVGcontext* context, int framebuffer_width,
        int framebuffer_height) override
    {
        nvgBeginFrame(context, static_cast<float>(framebuffer_width),
            static_cast<float>(framebuffer_height), 1.0f);
    }

    void scissor(
        NVGcontext* context, int x, int y, int width, int height) override
    {
        nvgScissor(context, static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(width), static_cast<float>(height));
    }

    void translate(NVGcontext* context, int x, int y) override
    {
        nvgTranslate(context, static_cast<float>(x), static_cast<float>(y));
    }

    void end_frame(NVGcontext* context) override { nvgEndFrame(context); }
};

class PluginNanoVGPass final : public IPluginNanoVGPass
{
public:
    PluginNanoVGPass(std::unique_ptr<detail::PluginNanoVGNativeBackend> backend,
        std::unique_ptr<detail::PluginNanoVGFrameOperations> operations)
        : backend_(std::move(backend))
        , operations_(std::move(operations))
    {
    }

    ~PluginNanoVGPass() override
    {
        // A callback may own product resources that refer to the drawing
        // context. Preserve the former adapters' explicit native teardown
        // before releasing callback captures.
        backend_.reset();
        draw_callback_ = nullptr;
    }

    void set_draw_callback(PluginNanoVGDrawFn callback) override
    {
        draw_callback_ = std::move(callback);
    }

    bool render_vulkan(const DraxulPluginVulkanFrameV2& frame) override
    {
        if (!draw_callback_)
            return true;
        NVGcontext* context = backend_->prepare_vulkan(frame);
        if (!context)
            return false;
        return render(context, frame.framebuffer_width, frame.framebuffer_height,
            frame.viewport);
    }

    bool render_metal(const DraxulPluginMetalFrameV2& frame) override
    {
        if (!draw_callback_)
            return true;
        NVGcontext* context = backend_->prepare_metal(frame);
        if (!context)
            return false;
        return render(context, frame.framebuffer_width, frame.framebuffer_height,
            frame.viewport);
    }

private:
    bool render(NVGcontext* context, int framebuffer_width,
        int framebuffer_height, const DraxulPluginViewportV2& viewport)
    {
        const int width = std::max(0, viewport.width);
        const int height = std::max(0, viewport.height);
        operations_->begin_frame(context, framebuffer_width, framebuffer_height);
        operations_->scissor(
            context, viewport.x, viewport.y, width, height);
        if (viewport.x != 0 || viewport.y != 0)
            operations_->translate(context, viewport.x, viewport.y);
        draw_callback_(context, width, height);
        operations_->end_frame(context);
        draw_callback_ = nullptr;
        return true;
    }

    PluginNanoVGDrawFn draw_callback_;
    std::unique_ptr<detail::PluginNanoVGNativeBackend> backend_;
    std::unique_ptr<detail::PluginNanoVGFrameOperations> operations_;
};

} // namespace

std::unique_ptr<IPluginNanoVGPass> create_plugin_nanovg_pass(
    PluginNanoVGPassOptions options)
{
    auto backend = detail::create_native_backend(options);
    if (!backend)
        return nullptr;
    return std::make_unique<PluginNanoVGPass>(
        std::move(backend), std::make_unique<NativeFrameOperations>());
}

namespace detail
{

std::unique_ptr<IPluginNanoVGPass> create_plugin_nanovg_pass_for_test(
    std::unique_ptr<PluginNanoVGNativeBackend> backend,
    std::unique_ptr<PluginNanoVGFrameOperations> operations)
{
    if (!backend || !operations)
        return nullptr;
    return std::make_unique<PluginNanoVGPass>(
        std::move(backend), std::move(operations));
}

} // namespace detail
} // namespace draxul::plugin_support
