#pragma once
#include <draxul/pending_atlas_upload.h>
#include <draxul/renderer.h>
#include <draxul/renderer_state.h>
#include <draxul/thread_check.h>
#include <optional>
#include <utility>
#include <vector>

// This header references Objective-C types directly and must only be
// included from Objective-C++ translation units. Plain C++ TUs that need
// to construct a MetalRenderer should instead include
// "metal/metal_renderer_factory.h" and call create_metal_renderer(), which
// returns the concrete instance through the public IGridRenderer interface.
// Forcing ObjC++-only inclusion eliminates the previous dual-layout hazard
// where MetalRenderer's data members had different types (ObjCRef<...> vs.
// void*) in different TUs, silently diverging the struct layout.
#ifndef __OBJC__
#error "metal_renderer.h must only be included from Objective-C++ (.mm) sources. Use metal_renderer_factory.h from plain C++."
#endif

#import <dispatch/dispatch.h>
#include <draxul/metal/objc_ref.h>
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLCommandBuffer;
@protocol MTLRenderCommandEncoder;
@protocol MTLRenderPipelineState;
@protocol MTLBuffer;
@protocol MTLTexture;
@protocol MTLSamplerState;
@class CAMetalLayer;
@protocol CAMetalDrawable;

namespace draxul
{

// MetalGridHandle is fully defined in metal_renderer.mm (ObjC++ only).
class MetalGridHandle;

class MetalRenderer : public IGridRenderer, public IImGuiHost, public ICaptureRenderer
{
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    explicit MetalRenderer(int atlas_size = kAtlasSize, RendererOptions options = {});
    ~MetalRenderer() override;

    bool initialize(IWindow& window) override;
    void shutdown() override;
    IFrameContext* begin_frame() override;
    void end_frame() override;
    std::unique_ptr<IGridHandle> create_grid_handle() override;
    void set_atlas_texture(const uint8_t* data, int w, int h) override;
    void update_atlas_region(int x, int y, int w, int h, const uint8_t* data) override;
    void resize(int pixel_w, int pixel_h) override;
    std::pair<int, int> cell_size_pixels() const override;
    void set_cell_size(int w, int h) override;
    void set_ascender(int a) override;
    bool initialize_imgui_backend() override;
    void shutdown_imgui_backend() override;
    void rebuild_imgui_font_texture() override;
    void begin_imgui_frame() override;
    bool render_imgui_draw_data(const ImDrawData* draw_data,
        ImGuiContext* context) override
    {
        return render_imgui_now(draw_data, context);
    }
    void request_frame_capture() override;
    std::optional<CapturedFrame> take_captured_frame() override;
    int padding() const override
    {
        return padding_;
    }
    void set_default_background(Color bg) override;
    void wait_idle() override;

private:
    class FrameContext;
    friend class MetalGridHandle;

    void flush_pending_atlas_uploads(void* cmd_buf);
    bool ensure_capture_buffer(size_t width, size_t height);
    bool ensure_depth_texture();
    bool ensure_main_render_encoder(bool with_depth);
    void end_main_render_encoder();
    void reset_full_window_scissor() const;
    void reset_full_window_viewport() const;
    bool start_new_chunk_command_buffer();
    bool flush_submit_chunk(bool final_chunk);
    bool draw_grid_handle_now(IGridHandle& handle);
    bool record_render_pass_now(IRenderPass& pass, const RenderViewport& viewport);
    bool render_imgui_now(const ImDrawData* draw_data, ImGuiContext* context);

    // Non-owning list of active grid handles; handles register/deregister themselves.
    std::vector<MetalGridHandle*> grid_handles_;

    // Metal object handles — ARC-managed via ObjCRef. This header is
    // ObjC++-only (see #error guard above), so a single set of typed
    // declarations is sufficient.
    ObjCRef<id<MTLDevice>> device_;
    ObjCRef<id<MTLCommandQueue>> command_queue_;
    ObjCRef<CAMetalLayer*> layer_;
    ObjCRef<id<MTLRenderPipelineState>> bg_pipeline_;
    ObjCRef<id<MTLRenderPipelineState>> fg_pipeline_;
    ObjCRef<id<MTLTexture>> atlas_texture_;
    ObjCRef<id<MTLTexture>> depth_texture_;
    ObjCRef<id<MTLSamplerState>> atlas_sampler_;
    ObjCRef<dispatch_semaphore_t> frame_semaphore_;
    ObjCRef<id<MTLBuffer>> capture_buffer_;
    ObjCRef<id<CAMetalDrawable>> current_drawable_;
    ObjCRef<id<MTLBuffer>> atlas_staging_[MAX_FRAMES_IN_FLIGHT];

    size_t atlas_staging_sizes_[MAX_FRAMES_IN_FLIGHT] = {};
    std::vector<PendingAtlasUpload> pending_atlas_uploads_;

    int atlas_size_ = kAtlasSize;
    int cell_w_ = 10;
    int cell_h_ = 20;
    int ascender_ = 16;
    int padding_ = 4;
    int pixel_w_ = 0;
    int pixel_h_ = 0;

    uint32_t current_frame_ = 0;

    float clear_r_ = 0.1f;
    float clear_g_ = 0.1f;
    float clear_b_ = 0.1f;

    bool capture_requested_ = false;
    std::optional<CapturedFrame> captured_frame_;
    size_t capture_buffer_size_ = 0;
    size_t capture_bytes_per_row_ = 0;

    bool imgui_initialized_ = false;
    bool wait_for_vblank_ = true;
    MainThreadChecker thread_checker_;
    std::unique_ptr<FrameContext> frame_context_;
    ObjCRef<id<MTLCommandBuffer>> active_command_buffer_;
    ObjCRef<id<MTLRenderCommandEncoder>> active_encoder_;
    bool frame_active_ = false;
    bool main_render_encoder_started_ = false;
    bool active_encoder_has_depth_ = false;
    bool chunk_has_work_ = false;
    bool submitted_chunk_ = false;
    uint64_t target_generation_ = 1;
};

} // namespace draxul
