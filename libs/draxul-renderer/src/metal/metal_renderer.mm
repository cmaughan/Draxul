#include "metal_renderer.h"
#include "metal_render_context.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <backends/imgui_impl_metal.h>
#include <cstring>
#include <draxul/log.h>
#include <draxul/pane_descriptor.h>
#include <draxul/window.h>
#include <imgui.h>

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
// MetalGridHandle — per-host grid handle owning pane-local cell state.
// MetalRenderer repacks all active handles into the current frame slot's buffer.
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

    RendererState state_;
    PaneDescriptor descriptor_;
    float scroll_offset_px_ = 0.f;

private:
    MetalRenderer& renderer_;
    int padding_ = 4;
};

// ---------------------------------------------------------------------------
// MetalRenderer
// ---------------------------------------------------------------------------

MetalRenderer::MetalRenderer(int atlas_size, RendererOptions options)
    : atlas_size_(atlas_size)
    , wait_for_vblank_(options.wait_for_vblank)
{
}

MetalRenderer::~MetalRenderer() = default;

void MetalRenderer::upload_dirty_state()
{
    size_t total_size = 0;
    for (auto* handle : grid_handles_)
        total_size += handle->state_.buffer_size_bytes();

    if (total_size == 0)
        return;

    // Ensure the current frame's shared grid buffer is large enough.
    const uint32_t slot = current_frame_ % MAX_FRAMES_IN_FLIGHT;
    if (grid_buffer_sizes_[slot] < total_size)
    {
        grid_buffers_[slot].reset([device_.get() newBufferWithLength:total_size
                                                             options:MTLResourceStorageModeShared]);
        grid_buffer_sizes_[slot] = total_size;
    }

    // Pack all handles sequentially into the shared buffer (matches Vulkan).
    auto* mapped = static_cast<std::byte*>([grid_buffers_[slot].get() contents]);
    if (!mapped)
        return;

    size_t byte_offset = 0;
    for (auto* handle : grid_handles_)
    {
        handle->state_.copy_to(mapped + byte_offset);
        handle->state_.clear_dirty();
        byte_offset += handle->state_.buffer_size_bytes();
    }
}

bool MetalRenderer::ensure_capture_buffer(size_t width, size_t height)
{
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
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        // BG pass: no blending (opaque)
        desc.colorAttachments[0].blendingEnabled = NO;

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
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
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
    for (auto& buf : grid_buffers_)
        buf.reset();
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
    return std::make_unique<MetalGridHandle>(*this, padding_);
}

void MetalRenderer::set_atlas_texture(const uint8_t* data, int w, int h)
{
    thread_checker_.assert_main_thread("MetalRenderer::set_atlas_texture");
    queue_full_atlas_upload(pending_atlas_uploads_, data, w, h);
}

void MetalRenderer::update_atlas_region(int x, int y, int w, int h, const uint8_t* data)
{
    thread_checker_.assert_main_thread("MetalRenderer::update_atlas_region");
    queue_atlas_region_upload(pending_atlas_uploads_, x, y, w, h, data);
}

void MetalRenderer::resize(int pixel_w, int pixel_h)
{
    pixel_w_ = pixel_w;
    pixel_h_ = pixel_h;
    layer_.get().drawableSize = CGSizeMake(pixel_w, pixel_h);
    depth_texture_.reset();
    ensure_depth_texture();
}

std::pair<int, int> MetalRenderer::cell_size_pixels() const
{
    return { cell_w_, cell_h_ };
}

void MetalRenderer::set_cell_size(int w, int h)
{
    cell_w_ = w;
    cell_h_ = h;
    for (auto* handle : grid_handles_)
        handle->state_.set_cell_size(w, h);
}

void MetalRenderer::set_ascender(int a)
{
    ascender_ = a;
    for (auto* handle : grid_handles_)
        handle->state_.set_ascender(a);
}

void MetalRenderer::set_default_background(Color bg)
{
    clear_r_ = bg.r;
    clear_g_ = bg.g;
    clear_b_ = bg.b;
}

bool MetalRenderer::initialize_imgui_backend()
{
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
    imgui_draw_data_ = nullptr;
    imgui_initialized_ = false;

    if (ImGui::GetCurrentContext() == nullptr)
        return;
    if (ImGui::GetIO().BackendRendererUserData == nullptr)
        return;

    ImGui_ImplMetal_Shutdown();
}

void MetalRenderer::rebuild_imgui_font_texture()
{
    if (!imgui_initialized_)
        return;

    ImGui_ImplMetal_DestroyFontsTexture();
    ImGui_ImplMetal_CreateFontsTexture(device_.get());
}

void MetalRenderer::begin_imgui_frame()
{
    if (!imgui_initialized_ || !current_drawable_)
        return;

    id<CAMetalDrawable> drawable = current_drawable_.get();
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = drawable.texture;
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (ensure_depth_texture())
    {
        rpDesc.depthAttachment.texture = depth_texture_.get();
        rpDesc.depthAttachment.loadAction = MTLLoadActionDontCare;
        rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
    }
    ImGui_ImplMetal_NewFrame(rpDesc);
}

void MetalRenderer::set_imgui_draw_data(const ImDrawData* draw_data)
{
    imgui_draw_data_ = draw_data;
}

void MetalRenderer::request_frame_capture()
{
    capture_requested_ = true;
}

std::optional<CapturedFrame> MetalRenderer::take_captured_frame()
{
    auto frame = std::move(captured_frame_);
    captured_frame_.reset();
    return frame;
}

bool MetalRenderer::begin_frame()
{
    thread_checker_.assert_main_thread("MetalRenderer::begin_frame");
    dispatch_semaphore_t sema = frame_semaphore_.get();
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    for (auto* handle : grid_handles_)
        handle->state_.restore_cursor();
    upload_dirty_state();

    id<CAMetalDrawable> drawable = [layer_.get() nextDrawable];
    if (!drawable)
    {
        dispatch_semaphore_signal(sema);
        return false;
    }
    current_drawable_.reset(drawable);

    return true;
}

void MetalRenderer::flush_pending_atlas_uploads(void* cmd_buf_opaque)
{
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
    for (auto* handle : grid_handles_)
        handle->state_.apply_cursor();
    upload_dirty_state();

    id<CAMetalDrawable> drawable = current_drawable_.take();

    id<MTLCommandBuffer> cmdBuf = [command_queue_.get() commandBuffer];

    // Flush any queued atlas uploads via blit encoder before the render pass samples the atlas.
    flush_pending_atlas_uploads((__bridge void*)cmdBuf);

    // Run any registered pre-pass (e.g. GBuffer) before the main render pass.
    if (render_pass_)
    {
        int vx = viewport3d_x_;
        int vy = viewport3d_y_;
        int vw = viewport3d_w_ > 0 ? viewport3d_w_ : pixel_w_;
        int vh = viewport3d_h_ > 0 ? viewport3d_h_ : pixel_h_;
        MetalRenderContext prepass_ctx(cmdBuf, nil, current_frame_, MAX_FRAMES_IN_FLIGHT,
            pixel_w_, pixel_h_, vx, vy, vw, vh);
        render_pass_->record_prepass(prepass_ctx);
    }

    // Create render pass descriptor
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = drawable.texture;
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(clear_r_, clear_g_, clear_b_, 1.0);
    if (ensure_depth_texture())
    {
        rpDesc.depthAttachment.texture = depth_texture_.get();
        rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
        rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
        rpDesc.depthAttachment.clearDepth = 1.0;
    }

    id<MTLRenderCommandEncoder> encoder = [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];

    id<MTLTexture> atlasTex = atlas_texture_.get();
    id<MTLSamplerState> sampler = atlas_sampler_.get();
    id<MTLBuffer> gridBuf = grid_buffers_[current_frame_ % MAX_FRAMES_IN_FLIGHT].get();

    // Draw each active grid handle using instance offsets into the shared buffer.
    NSUInteger instance_offset = 0;
    for (auto* handle : grid_handles_)
    {
        const int bg_instances = handle->state_.bg_instances();
        const int fg_instances = handle->state_.fg_instances();
        const NSUInteger handle_span = static_cast<NSUInteger>(handle->state_.buffer_size_bytes() / sizeof(GpuCell));

        if (bg_instances <= 0 || !gridBuf)
        {
            instance_offset += handle_span;
            continue;
        }

        // Set scissor rect to clip this host to its screen region
        const PaneDescriptor& desc = handle->descriptor_;
        if (desc.pixel_size.x > 0 && desc.pixel_size.y > 0)
        {
            MTLScissorRect scissor;
            scissor.x = static_cast<NSUInteger>(std::max(0, desc.pixel_pos.x));
            scissor.y = static_cast<NSUInteger>(std::max(0, desc.pixel_pos.y));
            scissor.width = static_cast<NSUInteger>(std::min(desc.pixel_size.x,
                pixel_w_ - std::max(0, desc.pixel_pos.x)));
            scissor.height = static_cast<NSUInteger>(std::min(desc.pixel_size.y,
                pixel_h_ - std::max(0, desc.pixel_pos.y)));
            [encoder setScissorRect:scissor];
        }

        // Push constants with viewport offset
        struct
        {
            float screen_w, screen_h, cell_w, cell_h, scroll_offset_px, viewport_x, viewport_y;
        } push_data = {
            (float)pixel_w_, (float)pixel_h_,
            (float)cell_w_, (float)cell_h_,
            handle->scroll_offset_px_,
            (float)desc.pixel_pos.x,
            (float)desc.pixel_pos.y
        };

        // Background pass — shared buffer with baseInstance offset
        [encoder setRenderPipelineState:bg_pipeline_.get()];
        [encoder setVertexBuffer:gridBuf offset:0 atIndex:0];
        [encoder setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:static_cast<NSUInteger>(bg_instances)
                   baseInstance:instance_offset];

        // Foreground pass
        [encoder setRenderPipelineState:fg_pipeline_.get()];
        [encoder setVertexBuffer:gridBuf offset:0 atIndex:0];
        [encoder setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
        [encoder setFragmentTexture:atlasTex atIndex:0];
        [encoder setFragmentSamplerState:sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:static_cast<NSUInteger>(fg_instances)
                   baseInstance:instance_offset];

        instance_offset += handle_span;
    }

    // Reset scissor to full window before 3D passes / ImGui
    if (pixel_w_ > 0 && pixel_h_ > 0)
    {
        MTLScissorRect full_scissor;
        full_scissor.x = 0;
        full_scissor.y = 0;
        full_scissor.width = static_cast<NSUInteger>(pixel_w_);
        full_scissor.height = static_cast<NSUInteger>(pixel_h_);
        [encoder setScissorRect:full_scissor];
    }

    if (render_pass_)
    {
        int vx = viewport3d_x_;
        int vy = viewport3d_y_;
        int vw = viewport3d_w_ > 0 ? viewport3d_w_ : pixel_w_;
        int vh = viewport3d_h_ > 0 ? viewport3d_h_ : pixel_h_;
        MetalRenderContext ctx(cmdBuf, encoder, current_frame_, MAX_FRAMES_IN_FLIGHT,
            pixel_w_, pixel_h_, vx, vy, vw, vh);
        render_pass_->record(ctx);
    }

    if (imgui_initialized_ && imgui_draw_data_)
        ImGui_ImplMetal_RenderDrawData(const_cast<ImDrawData*>(imgui_draw_data_), cmdBuf, encoder);

    [encoder endEncoding];

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
        }
        else
        {
            capture_requested_ = false;
        }
    }

    [cmdBuf presentDrawable:drawable];

    // Signal semaphore when GPU is done
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

    imgui_draw_data_ = nullptr;
    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void MetalRenderer::register_render_pass(std::shared_ptr<IRenderPass> pass)
{
    render_pass_ = std::move(pass);
}

void MetalRenderer::unregister_render_pass()
{
    render_pass_.reset();
}

void MetalRenderer::set_3d_viewport(int x, int y, int w, int h)
{
    viewport3d_x_ = x;
    viewport3d_y_ = y;
    viewport3d_w_ = w;
    viewport3d_h_ = h;
}

} // namespace draxul
