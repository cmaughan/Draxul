#pragma once
#include <draxul/renderer.h>
#include <draxul/renderer_state.h>
#include <draxul/thread_check.h>
#include <optional>
#include <vector>

#ifdef __OBJC__
#include "objc_ref.h"
#import <dispatch/dispatch.h>
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLRenderPipelineState;
@protocol MTLBuffer;
@protocol MTLTexture;
@protocol MTLSamplerState;
@class CAMetalLayer;
@protocol CAMetalDrawable;
#else
typedef void* id; // NOSONAR — forward declaration for ObjC 'id' type in non-ObjC translation units
#endif

namespace draxul
{

// MetalGridHandle is fully defined in metal_renderer.mm (ObjC++ only).
class MetalGridHandle;

class MetalRenderer : public IGridRenderer, public IImGuiHost, public ICaptureRenderer
{
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    explicit MetalRenderer(int atlas_size = kAtlasSize);
    ~MetalRenderer();

    bool initialize(IWindow& window) override;
    void shutdown() override;
    bool begin_frame() override;
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
    void set_imgui_draw_data(const ImDrawData* draw_data) override;
    void request_frame_capture() override;
    std::optional<CapturedFrame> take_captured_frame() override;
    int padding() const override
    {
        return padding_;
    }
    void set_default_background(Color bg) override;

    // I3DRenderer
    void register_render_pass(std::shared_ptr<IRenderPass> pass) override;
    void unregister_render_pass() override;
    void set_3d_viewport(int x, int y, int w, int h) override;

private:
    friend class MetalGridHandle;

    void upload_dirty_state();
    bool ensure_capture_buffer(size_t width, size_t height);
    bool ensure_depth_texture();

    // Non-owning list of active grid handles; handles register/deregister themselves.
    std::vector<MetalGridHandle*> grid_handles_;

    // Metal object handles — typed under ObjC++ (ARC-managed via ObjCRef),
    // opaque void* in plain C++ translation units.
#ifdef __OBJC__
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
#else
    void* device_ = nullptr; // NOSONAR cpp:S5008
    void* command_queue_ = nullptr; // NOSONAR cpp:S5008
    void* layer_ = nullptr; // NOSONAR cpp:S5008
    void* bg_pipeline_ = nullptr; // NOSONAR cpp:S5008
    void* fg_pipeline_ = nullptr; // NOSONAR cpp:S5008
    void* atlas_texture_ = nullptr; // NOSONAR cpp:S5008
    void* depth_texture_ = nullptr; // NOSONAR cpp:S5008
    void* atlas_sampler_ = nullptr; // NOSONAR cpp:S5008
    void* frame_semaphore_ = nullptr; // NOSONAR cpp:S5008
    void* capture_buffer_ = nullptr; // NOSONAR cpp:S5008
    void* current_drawable_ = nullptr; // NOSONAR cpp:S5008
#endif

    int atlas_size_ = kAtlasSize;
    int cell_w_ = 10;
    int cell_h_ = 20;
    int ascender_ = 16;
    int padding_ = 4;
    int pixel_w_ = 0;
    int pixel_h_ = 0;

    // Pane viewport for 3D render passes; (0,0,0,0) means full framebuffer.
    int viewport3d_x_ = 0;
    int viewport3d_y_ = 0;
    int viewport3d_w_ = 0;
    int viewport3d_h_ = 0;

    float clear_r_ = 0.1f;
    float clear_g_ = 0.1f;
    float clear_b_ = 0.1f;

    bool capture_requested_ = false;
    std::optional<CapturedFrame> captured_frame_;
    size_t capture_buffer_size_ = 0;
    size_t capture_bytes_per_row_ = 0;

    const ImDrawData* imgui_draw_data_ = nullptr;
    bool imgui_initialized_ = false;

    std::shared_ptr<IRenderPass> render_pass_;
    MainThreadChecker thread_checker_;
};

} // namespace draxul
