#pragma once

#include <draxul/plugin_gpu_imgui.h>
#include <draxul/satview/satview_runtime.h>

#include <imgui.h>

namespace draxul::satview::plugin
{

class DeferredOverlaySink final : public SatViewFrameSink
{
public:
    explicit DeferredOverlaySink(
        draxul::plugin_support::GpuImGuiHost* overlay)
        : overlay_(overlay)
    {
    }

    void record_scene(SatViewScenePass& pass, int x, int y,
        int width, int height) override
    {
        scene_pass_ = &pass;
        scene_x_ = x;
        scene_y_ = y;
        scene_width_ = width;
        scene_height_ = height;
    }
    void render_overlay(void* draw_data, void* context) override
    {
        draw_data_ = draw_data;
        context_ = context;
    }
    void finish() override {}

    void render()
    {
        if (overlay_ && draw_data_ && context_)
        {
            overlay_->render_imgui_draw_data(
                static_cast<ImDrawData*>(draw_data_),
                static_cast<ImGuiContext*>(context_));
        }
    }

    SatViewScenePass* scene_pass() const { return scene_pass_; }
    int scene_x() const { return scene_x_; }
    int scene_y() const { return scene_y_; }
    int scene_width() const { return scene_width_; }
    int scene_height() const { return scene_height_; }

private:
    draxul::plugin_support::GpuImGuiHost* overlay_ = nullptr;
    void* draw_data_ = nullptr;
    void* context_ = nullptr;
    SatViewScenePass* scene_pass_ = nullptr;
    int scene_x_ = 0;
    int scene_y_ = 0;
    int scene_width_ = 0;
    int scene_height_ = 0;
};

} // namespace draxul::satview::plugin
