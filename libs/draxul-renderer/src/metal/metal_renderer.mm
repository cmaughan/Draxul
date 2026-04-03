#include "metal_renderer.h"
#include "metal_render_context.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <backends/imgui_impl_metal.h>
#include <cstring>
#include <draxul/log.h>
#include <draxul/pane_descriptor.h>
#include <draxul/perf_timing.h>
#include <draxul/window.h>
#include <imgui.h>

#import <CoreGraphics/CoreGraphics.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace draxul
{

namespace
{

NSUInteger align_capture_row_bytes(NSUInteger width)
{
    const NSUInteger raw = width * 4;
    const NSUInteger alignment = 256;
    return ((raw + alignment - 1) / alignment) * alignment;
}

} // namespace

// ---------------------------------------------------------------------------
// MetalGridHandle — per-host grid handle owning pane-local cell state and
// per-frame GPU buffers.
// ---------------------------------------------------------------------------
class MetalGridHandle final : public IGridHandle
{
public:
    explicit MetalGridHandle(MetalRenderer& renderer, int padding)
        : renderer_(renderer)
        , padding_(padding)
    {
        renderer_.grid_handles_.push_back(this);
    }

    ~MetalGridHandle() override
    {
        auto& handles = renderer_.grid_handles_;
        handles.erase(std::remove(handles.begin(), handles.end(), this), handles.end());
    }

    void set_grid_size(int cols, int rows) override
    {
        state_.set_grid_size(cols, rows, padding_);
    }

    void update_cells(std::span<const CellUpdate> updates) override
    {
        state_.update_cells(updates);
    }

    void set_overlay_cells(std::span<const CellUpdate> updates) override
    {
        state_.set_overlay_cells(updates);
    }

    void set_cursor(int col, int row, const CursorStyle& style) override
    {
        state_.set_cursor(col, row, style);
    }

    void set_cursor_visible(bool visible) override
    {
        state_.set_cursor_visible(visible);
    }

    void set_default_background(Color bg) override
    {
        state_.set_default_background(bg);
    }

    void set_scroll_offset(float px) override
    {
        scroll_offset_px_ = px;
    }

    void set_viewport(const PaneDescriptor& desc) override
    {
        descriptor_ = desc;
    }

    void upload_state(uint32_t frame_index)
    {
        const size_t required_size = state_.buffer_size_bytes();
        if (required_size == 0)
            return;

        const uint32_t slot = frame_index % MetalRenderer::MAX_FRAMES_IN_FLIGHT;
        if (buffer_sizes_[slot] < required_size)
        {
            buffers_[slot].reset([renderer_.device_.get() newBufferWithLength:required_size
                                                                      options:MTLResourceStorageModeShared]);
            buffer_sizes_[slot] = required_size;
        }

        auto* mapped = static_cast<std::byte*>([buffers_[slot].get() contents]);
        if (!mapped)
            return;

        state_.copy_to(mapped);
        state_.clear_dirty();
    }

    id<MTLBuffer> current_buffer(uint32_t frame_index) const
    {
        return buffers_[frame_index % MetalRenderer::MAX_FRAMES_IN_FLIGHT].get();
    }

    RendererState state_;
    PaneDescriptor descriptor_;
    float scroll_offset_px_ = 0.f;

private:
    MetalRenderer& renderer_;
    int padding_ = 4;
    std::array<ObjCRef<id<MTLBuffer>>, MetalRenderer::MAX_FRAMES_IN_FLIGHT> buffers_;
    std::array<size_t, MetalRenderer::MAX_FRAMES_IN_FLIGHT> buffer_sizes_{};
};

class MetalRenderer::FrameContext final : public IFrameContext
{
public:
    explicit FrameContext(MetalRenderer& renderer)
        : renderer_(renderer)
    {
    }

    void draw_grid_handle(IGridHandle& handle) override
    {
        renderer_.draw_grid_handle_now(handle);
    }

    void record_render_pass(IRenderPass& pass, const RenderViewport& viewport) override
    {
        renderer_.record_render_pass_now(pass, viewport);
    }

    void render_imgui(const ImDrawData* draw_data, ImGuiContext* context) override
    {
        renderer_.render_imgui_now(draw_data, context);
    }

    void flush_submit_chunk() override
    {
        renderer_.flush_submit_chunk(false);
    }

private:
    MetalRenderer& renderer_;
};

// ---------------------------------------------------------------------------
// MetalRenderer
// ---------------------------------------------------------------------------

MetalRenderer::MetalRenderer(int atlas_size, RendererOptions options)
    : atlas_size_(atlas_size)
    , wait_for_vblank_(options.wait_for_vblank)
    , frame_context_(std::make_unique<FrameContext>(*this))
{
    PERF_MEASURE();
}

MetalRenderer::~MetalRenderer() = default;

bool MetalRenderer::ensure_capture_buffer(size_t width, size_t height)
{
    PERF_MEASURE();
    const size_t bytes_per_row = align_capture_row_bytes(static_cast<NSUInteger>(width));
    const size_t required_size = static_cast<size_t>(bytes_per_row * height);
    if (capture_buffer_ && required_size <= capture_buffer_size_ && bytes_per_row == capture_bytes_per_row_)
        return true;

    capture_buffer_.reset();

    id<MTLBuffer> buffer = [device_.get() newBufferWithLength:required_size options:MTLResourceStorageModeShared];
    if (!buffer)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Metal capture buffer");
        return false;
    }

    capture_buffer_.reset(buffer);
    capture_buffer_size_ = required_size;
    capture_bytes_per_row_ = bytes_per_row;
    return true;
}

bool MetalRenderer::ensure_depth_texture()
{
    PERF_MEASURE();
    if (!device_ || pixel_w_ <= 0 || pixel_h_ <= 0)
        return false;

    id<MTLTexture> existing = depth_texture_.get();
    if (existing && existing.width == static_cast<NSUInteger>(pixel_w_) && existing.height == static_cast<NSUInteger>(pixel_h_))
        return true;

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                    width:static_cast<NSUInteger>(pixel_w_)
                                                                                   height:static_cast<NSUInteger>(pixel_h_)
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> texture = [device_.get() newTextureWithDescriptor:desc];
    if (!texture)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Metal depth texture");
        return false;
    }

    depth_texture_.reset(texture);
    return true;
}

bool MetalRenderer::initialize(IWindow& window)
{
    PERF_MEASURE();
    current_frame_ = 0;

    // Get Metal device
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Metal device");
        return false;
    }
    device_.reset(device);

    // Get CAMetalLayer from SDL window
    SDL_MetalView metalView = SDL_Metal_CreateView(static_cast<SDL_Window*>(window.native_handle()));
    if (!metalView)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create Metal view: %s", SDL_GetError());
        return false;
    }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metalView);
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    CGColorSpaceRef srgb_color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (srgb_color_space)
    {
        layer.colorspace = srgb_color_space;
        CGColorSpaceRelease(srgb_color_space);
    }
    layer.framebufferOnly = NO;
    layer.displaySyncEnabled = wait_for_vblank_;
    layer_.reset(layer);

    auto [w, h] = window.size_pixels();
    pixel_w_ = w;
    pixel_h_ = h;
    layer.drawableSize = CGSizeMake(w, h);
    if (!ensure_depth_texture())
        return false;

    // Create command queue
    command_queue_.reset([device newCommandQueue]);

    // Load shader library from metallib
    NSError* error = nil;
    NSURL* libURL = [[NSBundle mainBundle] URLForResource:@"grid" withExtension:@"metallib"];
    id<MTLLibrary> library = nil;
    if (libURL)
    {
        library = [device newLibraryWithURL:libURL error:&error];
    }
    if (!library)
    {
        // Try loading from shaders/ directory next to executable
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* shaderPath = [exeDir stringByAppendingPathComponent:@"shaders/grid.metallib"];
        NSURL* shaderURL = [NSURL fileURLWithPath:shaderPath];
        library = [device newLibraryWithURL:shaderURL error:&error];
    }
    if (!library)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to load shader library: %s",
            error ? [[error localizedDescription] UTF8String] : "unknown");
        return false;
    }

    // Create background pipeline
    {
        id<MTLFunction> vertFunc = [library newFunctionWithName:@"bg_vertex"];
        id<MTLFunction> fragFunc = [library newFunctionWithName:@"bg_fragment"];
        if (!vertFunc || !fragFunc)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to find bg shader functions");
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertFunc;
        desc.fragmentFunction = fragFunc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        // BG pass: alpha blending so translucent background cells (e.g. command palette)
        // actually blend with the scene beneath them.
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create bg pipeline: %s",
                [[error localizedDescription] UTF8String]);
            return false;
        }
        bg_pipeline_.reset(pipeline);
    }

    // Create foreground pipeline
    {
        id<MTLFunction> vertFunc = [library newFunctionWithName:@"fg_vertex"];
        id<MTLFunction> fragFunc = [library newFunctionWithName:@"fg_fragment"];
        if (!vertFunc || !fragFunc)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to find fg shader functions");
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertFunc;
        desc.fragmentFunction = fragFunc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        // FG pass: alpha blending
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create fg pipeline: %s",
                [[error localizedDescription] UTF8String]);
            return false;
        }
        fg_pipeline_.reset(pipeline);
    }

    // Create atlas texture (RGBA so color glyphs can bypass fg tinting)
    {
        MTLTextureDescriptor* texDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:static_cast<NSUInteger>(atlas_size_)
                                        height:static_cast<NSUInteger>(atlas_size_)
                                     mipmapped:NO];
        texDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
        texDesc.storageMode = MTLStorageModePrivate;

        atlas_texture_.reset([device newTextureWithDescriptor:texDesc]);
    }

    // Create atlas sampler
    {
        MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
        sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;

        atlas_sampler_.reset([device newSamplerStateWithDescriptor:sampDesc]);
    }

    // Allow a small number of frames in flight and only block when reusing a frame slot.
    frame_semaphore_.reset(dispatch_semaphore_create(MAX_FRAMES_IN_FLIGHT));

    DRAXUL_LOG_INFO(LogCategory::Renderer, "Metal renderer initialized (%s)", [[device name] UTF8String]);
    return true;
}

void MetalRenderer::shutdown()
{
    PERF_MEASURE();
    // Wait for every frame slot to be returned before releasing shared resources.
    dispatch_semaphore_t sema = frame_semaphore_.get();
    if (sema)
    {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            dispatch_semaphore_signal(sema);
    }

    // Release Metal objects explicitly in reverse-dependency order.
    for (auto& staging : atlas_staging_)
        staging.reset();
    pending_atlas_uploads_.clear();
    atlas_sampler_.reset();
    atlas_texture_.reset();
    depth_texture_.reset();
    capture_buffer_.reset();
    fg_pipeline_.reset();
    bg_pipeline_.reset();
    command_queue_.reset();
    layer_.reset();
    frame_semaphore_.reset();
    device_.reset();
}

std::unique_ptr<IGridHandle> MetalRenderer::create_grid_handle()
{
    PERF_MEASURE();
    auto handle = std::make_unique<MetalGridHandle>(*this, padding_);
    handle->state_.set_cell_size(cell_w_, cell_h_);
    handle->state_.set_ascender(ascender_);
    return handle;
}

void MetalRenderer::set_atlas_texture(const uint8_t* data, int w, int h)
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("MetalRenderer::set_atlas_texture");
    queue_full_atlas_upload(pending_atlas_uploads_, data, w, h);
}

void MetalRenderer::update_atlas_region(int x, int y, int w, int h, const uint8_t* data)
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("MetalRenderer::update_atlas_region");
    queue_atlas_region_upload(pending_atlas_uploads_, x, y, w, h, data);
}

void MetalRenderer::resize(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    pixel_w_ = pixel_w;
    pixel_h_ = pixel_h;
    layer_.get().drawableSize = CGSizeMake(pixel_w, pixel_h);
    depth_texture_.reset();
    ensure_depth_texture();
}

std::pair<int, int> MetalRenderer::cell_size_pixels() const
{
    PERF_MEASURE();
    return { cell_w_, cell_h_ };
}

void MetalRenderer::set_cell_size(int w, int h)
{
    PERF_MEASURE();
    cell_w_ = w;
    cell_h_ = h;
    for (auto* handle : grid_handles_)
        handle->state_.set_cell_size(w, h);
}

void MetalRenderer::set_ascender(int a)
{
    PERF_MEASURE();
    ascender_ = a;
    for (auto* handle : grid_handles_)
        handle->state_.set_ascender(a);
}

void MetalRenderer::set_default_background(Color bg)
{
    PERF_MEASURE();
    clear_r_ = bg.r;
    clear_g_ = bg.g;
    clear_b_ = bg.b;
}

bool MetalRenderer::initialize_imgui_backend()
{
    PERF_MEASURE();
    // Guard per context: ImGui_ImplMetal_Init() stores backend data in the current
    // context's BackendRendererUserData. If the current context already has it, skip.
    if (ImGui::GetIO().BackendRendererUserData != nullptr)
        return true;

    if (!ImGui_ImplMetal_Init(device_.get()))
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to initialize ImGui Metal backend");
        return false;
    }

    imgui_initialized_ = true;
    return true;
}

void MetalRenderer::shutdown_imgui_backend()
{
    PERF_MEASURE();
    imgui_initialized_ = false;

    if (ImGui::GetCurrentContext() == nullptr)
        return;
    if (ImGui::GetIO().BackendRendererUserData == nullptr)
        return;

    ImGui_ImplMetal_Shutdown();
}

void MetalRenderer::rebuild_imgui_font_texture()
{
    PERF_MEASURE();
    if (!imgui_initialized_)
        return;

    ImGui_ImplMetal_DestroyFontsTexture();
    ImGui_ImplMetal_CreateFontsTexture(device_.get());
}

void MetalRenderer::begin_imgui_frame()
{
    PERF_MEASURE();
    if (!imgui_initialized_ || !current_drawable_)
        return;

    id<CAMetalDrawable> drawable = current_drawable_.get();
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = drawable.texture;
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    ImGui_ImplMetal_NewFrame(rpDesc);
}

void MetalRenderer::request_frame_capture()
{
    PERF_MEASURE();
    capture_requested_ = true;
}

std::optional<CapturedFrame> MetalRenderer::take_captured_frame()
{
    PERF_MEASURE();
    auto frame = std::move(captured_frame_);
    captured_frame_.reset();
    return frame;
}

IFrameContext* MetalRenderer::begin_frame()
{
    thread_checker_.assert_main_thread("MetalRenderer::begin_frame");

    bool success = false;
    {
        PERF_MEASURE();
        dispatch_semaphore_t sema = frame_semaphore_.get();
        dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

        for (auto* handle : grid_handles_)
            handle->state_.restore_cursor();

        id<CAMetalDrawable> drawable = [layer_.get() nextDrawable];
        if (!drawable)
            dispatch_semaphore_signal(sema);
        else
        {
            current_drawable_.reset(drawable);
            id<MTLCommandBuffer> cmd_buf = [command_queue_.get() commandBuffer];
            if (!cmd_buf)
            {
                dispatch_semaphore_signal(sema);
            }
            else
            {
                active_command_buffer_.reset(cmd_buf);
                active_encoder_.reset(nil);
                active_encoder_has_depth_ = false;
                frame_active_ = true;
                main_render_encoder_started_ = false;
                chunk_has_work_ = !pending_atlas_uploads_.empty();
                submitted_chunk_ = false;
                flush_pending_atlas_uploads((__bridge void*)cmd_buf);
                success = true;
            }
        }
    }

    if (!success)
        runtime_perf_collector().cancel_frame();
    return success ? frame_context_.get() : nullptr;
}

bool MetalRenderer::start_new_chunk_command_buffer()
{
    if (!frame_active_)
        return false;

    id<MTLCommandBuffer> cmd_buf = [command_queue_.get() commandBuffer];
    if (!cmd_buf)
        return false;

    active_command_buffer_.reset(cmd_buf);
    active_encoder_.reset(nil);
    active_encoder_has_depth_ = false;
    chunk_has_work_ = false;
    return true;
}

bool MetalRenderer::flush_submit_chunk(bool final_chunk)
{
    if (!frame_active_ || !active_command_buffer_)
        return false;

    if (active_encoder_)
        end_main_render_encoder();

    if (!chunk_has_work_ && !final_chunk)
        return true;

    id<MTLCommandBuffer> cmdBuf = active_command_buffer_.get();
    if (final_chunk)
    {
        if (!chunk_has_work_)
        {
            if (!ensure_main_render_encoder(false))
                return false;
            end_main_render_encoder();
            chunk_has_work_ = true;
        }

        id<CAMetalDrawable> drawable = current_drawable_.take();
        if (!drawable)
            return false;

        if (capture_requested_)
        {
            const size_t width = drawable.texture.width;
            const size_t height = drawable.texture.height;
            if (ensure_capture_buffer(width, height))
            {
                id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
                [blit copyFromTexture:drawable.texture
                                 sourceSlice:0
                                 sourceLevel:0
                                sourceOrigin:MTLOriginMake(0, 0, 0)
                                  sourceSize:MTLSizeMake(width, height, 1)
                                    toBuffer:capture_buffer_.get()
                           destinationOffset:0
                      destinationBytesPerRow:capture_bytes_per_row_
                    destinationBytesPerImage:capture_bytes_per_row_ * height];
                [blit endEncoding];
                chunk_has_work_ = true;
            }
            else
            {
                capture_requested_ = false;
            }
        }

        [cmdBuf presentDrawable:drawable];

        dispatch_semaphore_t sema = frame_semaphore_.get();
        [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer>) {
            dispatch_semaphore_signal(sema);
        }];

        [cmdBuf commit];

        if (capture_requested_)
        {
            [cmdBuf waitUntilCompleted];

            CapturedFrame frame;
            frame.width = pixel_w_;
            frame.height = pixel_h_;
            frame.rgba.resize(static_cast<size_t>(frame.width) * frame.height * 4);

            const auto* src = static_cast<const uint8_t*>([capture_buffer_.get() contents]);
            if (src)
            {
                for (int y = 0; y < frame.height; ++y)
                {
                    const size_t src_row = static_cast<size_t>(y) * capture_bytes_per_row_;
                    const size_t dst_row = static_cast<size_t>(y) * frame.width * 4;
                    for (int x = 0; x < frame.width; ++x)
                    {
                        const size_t src_index = src_row + static_cast<size_t>(x * 4);
                        const size_t dst_index = dst_row + static_cast<size_t>(x * 4);
                        frame.rgba[dst_index + 0] = src[src_index + 2];
                        frame.rgba[dst_index + 1] = src[src_index + 1];
                        frame.rgba[dst_index + 2] = src[src_index + 0];
                        frame.rgba[dst_index + 3] = src[src_index + 3];
                    }
                }
                captured_frame_ = std::move(frame);
            }
            capture_requested_ = false;
        }

        active_command_buffer_.reset(nil);
        active_encoder_.reset(nil);
        active_encoder_has_depth_ = false;
        frame_active_ = false;
        main_render_encoder_started_ = false;
        chunk_has_work_ = false;
        submitted_chunk_ = false;
        current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
        return true;
    }

    [cmdBuf commit];
    submitted_chunk_ = true;
    if (!start_new_chunk_command_buffer())
    {
        runtime_perf_collector().cancel_frame();
        return false;
    }
    return true;
}

bool MetalRenderer::ensure_main_render_encoder(bool with_depth)
{
    if (!frame_active_ || !current_drawable_ || !active_command_buffer_)
        return false;
    if (active_encoder_)
    {
        if (active_encoder_has_depth_ == with_depth)
            return true;
        end_main_render_encoder();
    }

    id<CAMetalDrawable> drawable = current_drawable_.get();
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = drawable.texture;
    rpDesc.colorAttachments[0].loadAction = main_render_encoder_started_ ? MTLLoadActionLoad : MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(clear_r_, clear_g_, clear_b_, 1.0);
    if (with_depth)
    {
        if (!ensure_depth_texture())
            return false;
        rpDesc.depthAttachment.texture = depth_texture_.get();
        rpDesc.depthAttachment.loadAction = main_render_encoder_started_ ? MTLLoadActionLoad : MTLLoadActionClear;
        rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
        rpDesc.depthAttachment.clearDepth = 1.0;
    }

    active_encoder_.reset([active_command_buffer_.get() renderCommandEncoderWithDescriptor:rpDesc]);
    if (!active_encoder_)
        return false;

    main_render_encoder_started_ = true;
    active_encoder_has_depth_ = with_depth;
    reset_full_window_viewport();
    reset_full_window_scissor();
    return true;
}

void MetalRenderer::end_main_render_encoder()
{
    if (!active_encoder_)
        return;
    [active_encoder_.get() endEncoding];
    active_encoder_.reset(nil);
    active_encoder_has_depth_ = false;
}

void MetalRenderer::reset_full_window_scissor() const
{
    if (!active_encoder_ || pixel_w_ <= 0 || pixel_h_ <= 0)
        return;
    MTLScissorRect full_scissor;
    full_scissor.x = 0;
    full_scissor.y = 0;
    full_scissor.width = static_cast<NSUInteger>(pixel_w_);
    full_scissor.height = static_cast<NSUInteger>(pixel_h_);
    [active_encoder_.get() setScissorRect:full_scissor];
}

void MetalRenderer::reset_full_window_viewport() const
{
    if (!active_encoder_)
        return;
    MTLViewport full_viewport;
    full_viewport.originX = 0.0;
    full_viewport.originY = 0.0;
    full_viewport.width = static_cast<double>(pixel_w_);
    full_viewport.height = static_cast<double>(pixel_h_);
    full_viewport.znear = 0.0;
    full_viewport.zfar = 1.0;
    [active_encoder_.get() setViewport:full_viewport];
}

bool MetalRenderer::draw_grid_handle_now(IGridHandle& handle)
{
    auto* metal_handle = dynamic_cast<MetalGridHandle*>(&handle);
    if (!metal_handle || !frame_active_)
        return false;

    metal_handle->state_.apply_cursor();
    metal_handle->upload_state(current_frame_);

    if (!ensure_main_render_encoder(false))
        return false;

    const PaneDescriptor& desc = metal_handle->descriptor_;
    // Skip drawing entirely when the viewport has zero area (e.g. zoomed-out panes).
    if (desc.pixel_size.x <= 0 || desc.pixel_size.y <= 0)
        return true;

    const int bg_instances = metal_handle->state_.bg_instances();
    const int fg_instances = metal_handle->state_.fg_instances();
    id<MTLBuffer> grid_buf = metal_handle->current_buffer(current_frame_);
    if (bg_instances <= 0 || !grid_buf)
        return true;

    reset_full_window_viewport();

    {
        MTLScissorRect scissor;
        scissor.x = static_cast<NSUInteger>(std::max(0, desc.pixel_pos.x));
        scissor.y = static_cast<NSUInteger>(std::max(0, desc.pixel_pos.y));
        scissor.width = static_cast<NSUInteger>(std::min(desc.pixel_size.x,
            pixel_w_ - std::max(0, desc.pixel_pos.x)));
        scissor.height = static_cast<NSUInteger>(std::min(desc.pixel_size.y,
            pixel_h_ - std::max(0, desc.pixel_pos.y)));
        [active_encoder_.get() setScissorRect:scissor];
    }

    struct
    {
        float screen_w, screen_h, cell_w, cell_h, scroll_offset_px, viewport_x, viewport_y;
    } push_data = {
        static_cast<float>(pixel_w_), static_cast<float>(pixel_h_),
        static_cast<float>(cell_w_), static_cast<float>(cell_h_),
        metal_handle->scroll_offset_px_,
        static_cast<float>(desc.pixel_pos.x),
        static_cast<float>(desc.pixel_pos.y)
    };

    [active_encoder_.get() setRenderPipelineState:bg_pipeline_.get()];
    [active_encoder_.get() setVertexBuffer:grid_buf offset:0 atIndex:0];
    [active_encoder_.get() setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
    [active_encoder_.get() drawPrimitives:MTLPrimitiveTypeTriangle
                              vertexStart:0
                              vertexCount:6
                            instanceCount:static_cast<NSUInteger>(bg_instances)
                             baseInstance:0];

    [active_encoder_.get() setRenderPipelineState:fg_pipeline_.get()];
    [active_encoder_.get() setVertexBuffer:grid_buf offset:0 atIndex:0];
    [active_encoder_.get() setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
    [active_encoder_.get() setFragmentTexture:atlas_texture_.get() atIndex:0];
    [active_encoder_.get() setFragmentSamplerState:atlas_sampler_.get() atIndex:0];
    [active_encoder_.get() drawPrimitives:MTLPrimitiveTypeTriangle
                              vertexStart:0
                              vertexCount:6
                            instanceCount:static_cast<NSUInteger>(fg_instances)
                             baseInstance:0];
    chunk_has_work_ = true;
    return true;
}

bool MetalRenderer::record_render_pass_now(IRenderPass& pass, const RenderViewport& viewport)
{
    if (!frame_active_ || !active_command_buffer_)
        return false;

    // If no main render encoder has run yet this frame, the drawable has not been
    // cleared.  Issue a lightweight clear pass so the prepass (which uses
    // LoadAction::Load) starts from a known background rather than stale data.
    if (!main_render_encoder_started_)
    {
        end_main_render_encoder();
        if (!ensure_main_render_encoder(false))
            return false;
        end_main_render_encoder();
        // ensure_main_render_encoder set main_render_encoder_started_ = true,
        // so subsequent encoders will use LoadAction::Load.
    }
    else
    {
        end_main_render_encoder();
    }

    const int vx = viewport.x;
    const int vy = viewport.y;
    const int vw = viewport.width > 0 ? viewport.width : pixel_w_;
    const int vh = viewport.height > 0 ? viewport.height : pixel_h_;

    id<MTLTexture> drawable_tex = current_drawable_.get() ? [current_drawable_.get() texture] : nil;
    MetalRenderContext prepass_ctx(active_command_buffer_.get(), nil, current_frame_, MAX_FRAMES_IN_FLIGHT,
        pixel_w_, pixel_h_, vx, vy, vw, vh, device_.get(), drawable_tex);
    pass.record_prepass(prepass_ctx);

    // A prepass may have rendered directly to the drawable (e.g. NanoVG creates
    // its own render encoder).  Mark the drawable as touched so that the next
    // main render encoder uses LoadAction::Load instead of Clear.
    main_render_encoder_started_ = true;

    if (!ensure_main_render_encoder(pass.requires_main_depth_attachment()))
        return false;

    MTLViewport pass_viewport;
    pass_viewport.originX = static_cast<double>(vx);
    pass_viewport.originY = static_cast<double>(vy);
    pass_viewport.width = static_cast<double>(std::max(0, vw));
    pass_viewport.height = static_cast<double>(std::max(0, vh));
    pass_viewport.znear = 0.0;
    pass_viewport.zfar = 1.0;
    [active_encoder_.get() setViewport:pass_viewport];

    MTLScissorRect pass_scissor;
    pass_scissor.x = static_cast<NSUInteger>(std::max(0, vx));
    pass_scissor.y = static_cast<NSUInteger>(std::max(0, vy));
    pass_scissor.width = static_cast<NSUInteger>(std::max(0, std::min(vw, pixel_w_ - static_cast<int>(pass_scissor.x))));
    pass_scissor.height = static_cast<NSUInteger>(std::max(0, std::min(vh, pixel_h_ - static_cast<int>(pass_scissor.y))));
    [active_encoder_.get() setScissorRect:pass_scissor];

    MetalRenderContext ctx(active_command_buffer_.get(), active_encoder_.get(), current_frame_, MAX_FRAMES_IN_FLIGHT,
        pixel_w_, pixel_h_, vx, vy, vw, vh);
    pass.record(ctx);
    chunk_has_work_ = true;
    return true;
}

bool MetalRenderer::render_imgui_now(const ImDrawData* draw_data, ImGuiContext* context)
{
    if (!draw_data || !context || !frame_active_)
        return false;
    if (!ensure_main_render_encoder(false))
        return false;

    reset_full_window_viewport();
    reset_full_window_scissor();

    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context);
    ImGui_ImplMetal_RenderDrawData(const_cast<ImDrawData*>(draw_data), active_command_buffer_.get(), active_encoder_.get());
    ImGui::SetCurrentContext(prev_ctx);
    chunk_has_work_ = true;
    return true;
}

void MetalRenderer::flush_pending_atlas_uploads(void* cmd_buf_opaque)
{
    PERF_MEASURE();
    if (pending_atlas_uploads_.empty())
        return;

    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)cmd_buf_opaque;

    const size_t total_bytes = pending_atlas_upload_size_bytes(pending_atlas_uploads_);
    if (total_bytes == 0)
    {
        pending_atlas_uploads_.clear();
        return;
    }

    // Ensure per-frame staging buffer is large enough.
    const uint32_t slot = current_frame_ % MAX_FRAMES_IN_FLIGHT;
    if (atlas_staging_sizes_[slot] < total_bytes)
    {
        atlas_staging_[slot].reset([device_.get() newBufferWithLength:total_bytes
                                                              options:MTLResourceStorageModeShared]);
        atlas_staging_sizes_[slot] = total_bytes;
    }

    // Copy pixel data into the staging buffer.
    auto* dst = static_cast<uint8_t*>([atlas_staging_[slot].get() contents]);
    size_t offset = 0;
    for (const auto& upload : pending_atlas_uploads_)
    {
        if (upload.pixels.empty())
            continue;
        std::memcpy(dst + offset, upload.pixels.data(), upload.pixels.size());
        offset += upload.pixels.size();
    }

    // Encode blit commands: staging buffer → atlas texture.
    id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
    offset = 0;
    id<MTLTexture> atlasTex = atlas_texture_.get();
    for (const auto& upload : pending_atlas_uploads_)
    {
        if (upload.pixels.empty())
            continue;

        const NSUInteger bytes_per_row = static_cast<NSUInteger>(upload.w) * 4;
        [blit copyFromBuffer:atlas_staging_[slot].get()
                   sourceOffset:offset
              sourceBytesPerRow:bytes_per_row
            sourceBytesPerImage:upload.pixels.size()
                     sourceSize:MTLSizeMake(static_cast<NSUInteger>(upload.w),
                                    static_cast<NSUInteger>(upload.h), 1)
                      toTexture:atlasTex
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:MTLOriginMake(static_cast<NSUInteger>(upload.x),
                                    static_cast<NSUInteger>(upload.y), 0)];
        offset += upload.pixels.size();
    }
    [blit endEncoding];

    pending_atlas_uploads_.clear();
}

void MetalRenderer::end_frame()
{
    thread_checker_.assert_main_thread("MetalRenderer::end_frame");
    {
        PERF_MEASURE();
        if (!frame_active_ || !active_command_buffer_ || !current_drawable_)
            return;

        if (!chunk_has_work_ && !main_render_encoder_started_)
        {
            if (!ensure_main_render_encoder(false))
            {
                runtime_perf_collector().cancel_frame();
                active_command_buffer_.reset(nil);
                active_encoder_.reset(nil);
                active_encoder_has_depth_ = false;
                current_drawable_.reset(nil);
                frame_active_ = false;
                main_render_encoder_started_ = false;
                chunk_has_work_ = false;
                submitted_chunk_ = false;
                dispatch_semaphore_signal(frame_semaphore_.get());
                return;
            }
            end_main_render_encoder();
            chunk_has_work_ = true;
        }
        if (!flush_submit_chunk(true))
        {
            runtime_perf_collector().cancel_frame();
            active_command_buffer_.reset(nil);
            active_encoder_.reset(nil);
            active_encoder_has_depth_ = false;
            current_drawable_.reset(nil);
            frame_active_ = false;
            main_render_encoder_started_ = false;
            chunk_has_work_ = false;
            submitted_chunk_ = false;
            dispatch_semaphore_signal(frame_semaphore_.get());
        }
    }
}

} // namespace draxul
