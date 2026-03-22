#include "metal_renderer.h"
#include "metal_render_context.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <backends/imgui_impl_metal.h>
#include <cstring>
#include <draxul/log.h>
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

MetalRenderer::MetalRenderer(int atlas_size)
    : atlas_size_(atlas_size)
{
    // Always start with one pane (pane 0)
    panes_.emplace_back();
}
MetalRenderer::~MetalRenderer() = default;

size_t MetalRenderer::compute_total_buffer_cells() const
{
    size_t total = 0;
    for (const auto& pane : panes_)
    {
        if (pane.active)
            total += pane.state.total_cells() + RendererState::OVERLAY_CELL_CAPACITY + 1;
    }
    return total;
}

size_t MetalRenderer::pane_cell_offset(int pane_id) const
{
    size_t offset = 0;
    for (int i = 0; i < pane_id; ++i)
    {
        if (panes_[static_cast<size_t>(i)].active)
            offset += panes_[static_cast<size_t>(i)].state.total_cells() + RendererState::OVERLAY_CELL_CAPACITY + 1;
    }
    return offset;
}

void MetalRenderer::upload_dirty_state()
{
    id<MTLBuffer> buf = grid_buffer_.get();
    if (!buf)
        return;

    auto* bytes = static_cast<std::byte*>([buf contents]);
    for (int i = 0; i < static_cast<int>(panes_.size()); ++i)
    {
        auto& pane = panes_[static_cast<size_t>(i)];
        if (!pane.active)
            continue;
        const size_t pane_byte_offset = pane_cell_offset(i) * sizeof(GpuCell);

        if (pane.state.has_dirty_cells())
        {
            pane.state.copy_dirty_cells_to(bytes + pane_byte_offset + pane.state.dirty_cell_offset_bytes());
        }

        if (pane.state.overlay_region_dirty())
        {
            pane.state.copy_overlay_region_to(bytes + pane_byte_offset + pane.state.overlay_offset_bytes());
        }

        pane.state.clear_dirty();
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

bool MetalRenderer::initialize(IWindow& window)
{
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
    layer_.reset(layer);

    auto [w, h] = window.size_pixels();
    pixel_w_ = w;
    pixel_h_ = h;
    layer.drawableSize = CGSizeMake(w, h);

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
        texDesc.usage = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;

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

    // Create grid buffer (start with 80x24)
    {
        size_t initial_size = 80 * 24 * sizeof(GpuCell);
        grid_buffer_.reset([device newBufferWithLength:initial_size
                                               options:MTLResourceStorageModeShared]);
    }

    // Create frame semaphore for double buffering
    // Use count of 1 since we have a single shared grid buffer —
    // must wait for GPU to finish reading before CPU modifies it
    frame_semaphore_.reset(dispatch_semaphore_create(1));

    DRAXUL_LOG_INFO(LogCategory::Renderer, "Metal renderer initialized (%s)", [[device name] UTF8String]);
    return true;
}

void MetalRenderer::shutdown()
{
    // Wait for the in-flight frame to complete
    dispatch_semaphore_t sema = frame_semaphore_.get();
    if (sema)
    {
        dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
        dispatch_semaphore_signal(sema);
    }

    // Release Metal objects explicitly in reverse-dependency order.
    // The ObjCRef destructors handle the actual CFRelease.
    atlas_sampler_.reset();
    atlas_texture_.reset();
    grid_buffer_.reset();
    capture_buffer_.reset();
    fg_pipeline_.reset();
    bg_pipeline_.reset();
    command_queue_.reset();
    layer_.reset();
    frame_semaphore_.reset();
    device_.reset();
}

void MetalRenderer::set_grid_size(int cols, int rows)
{
    set_grid_size(0, cols, rows);
}

void MetalRenderer::update_cells(std::span<const CellUpdate> updates)
{
    update_cells(0, updates);
}

void MetalRenderer::set_overlay_cells(std::span<const CellUpdate> updates)
{
    set_overlay_cells(0, updates);
}

void MetalRenderer::set_atlas_texture(const uint8_t* data, int w, int h)
{
    id<MTLTexture> tex = atlas_texture_.get();
    MTLRegion region = MTLRegionMake2D(0, 0, w, h);
    [tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:w * 4];
}

void MetalRenderer::update_atlas_region(int x, int y, int w, int h, const uint8_t* data)
{
    id<MTLTexture> tex = atlas_texture_.get();
    MTLRegion region = MTLRegionMake2D(x, y, w, h);
    [tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:w * 4];
}

void MetalRenderer::set_cursor(int col, int row, const CursorStyle& style)
{
    set_cursor(0, col, row, style);
}

void MetalRenderer::resize(int pixel_w, int pixel_h)
{
    pixel_w_ = pixel_w;
    pixel_h_ = pixel_h;
    layer_.get().drawableSize = CGSizeMake(pixel_w, pixel_h);
}

std::pair<int, int> MetalRenderer::cell_size_pixels() const
{
    return { cell_w_, cell_h_ };
}

void MetalRenderer::set_cell_size(int w, int h)
{
    cell_w_ = w;
    cell_h_ = h;
    for (auto& pane : panes_)
    {
        if (pane.active)
            pane.state.set_cell_size(w, h);
    }
}

void MetalRenderer::set_ascender(int a)
{
    ascender_ = a;
    for (auto& pane : panes_)
    {
        if (pane.active)
            pane.state.set_ascender(a);
    }
}

void MetalRenderer::set_default_background(Color bg)
{
    clear_r_ = bg.r;
    clear_g_ = bg.g;
    clear_b_ = bg.b;
    set_default_background(0, bg);
}

void MetalRenderer::set_scroll_offset(float px)
{
    set_scroll_offset(0, px);
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

    // Guard per-context: only call Shutdown if the current context has a
    // backend attached. This allows the method to be called once per context
    // (e.g. MegaCityHost then UiPanel) without double-free or null-deref.
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
    dispatch_semaphore_t sema = frame_semaphore_.get();
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    for (auto& pane : panes_)
    {
        if (pane.active)
            pane.state.restore_cursor();
    }
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

void MetalRenderer::end_frame()
{
    for (auto& pane : panes_)
    {
        if (pane.active)
            pane.state.apply_cursor();
    }
    upload_dirty_state();

    id<CAMetalDrawable> drawable = current_drawable_.take();

    id<MTLCommandBuffer> cmdBuf = [command_queue_.get() commandBuffer];

    // Create render pass descriptor
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = drawable.texture;
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(clear_r_, clear_g_, clear_b_, 1.0);

    id<MTLRenderCommandEncoder> encoder = [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];

    id<MTLBuffer> gridBuf = grid_buffer_.get();
    id<MTLTexture> atlasTex = atlas_texture_.get();
    id<MTLSamplerState> sampler = atlas_sampler_.get();

    // Draw each active pane
    for (int pane_id = 0; pane_id < static_cast<int>(panes_.size()); ++pane_id)
    {
        const auto& pane = panes_[static_cast<size_t>(pane_id)];
        if (!pane.active)
            continue;

        const int bg_instances = pane.state.bg_instances();
        const int fg_instances = pane.state.fg_instances();
        if (bg_instances <= 0)
            continue;

        // Compute buffer offset for this pane's cells (in bytes)
        const NSUInteger pane_offset_bytes = pane_cell_offset(pane_id) * sizeof(GpuCell);

        // Set scissor rect to clip this pane to its screen region
        const PaneDescriptor& desc = pane.descriptor;
        if (desc.pixel_width > 0 && desc.pixel_height > 0)
        {
            MTLScissorRect scissor;
            scissor.x = static_cast<NSUInteger>(std::max(0, desc.pixel_x));
            scissor.y = static_cast<NSUInteger>(std::max(0, desc.pixel_y));
            scissor.width = static_cast<NSUInteger>(std::min(desc.pixel_width,
                pixel_w_ - std::max(0, desc.pixel_x)));
            scissor.height = static_cast<NSUInteger>(std::min(desc.pixel_height,
                pixel_h_ - std::max(0, desc.pixel_y)));
            [encoder setScissorRect:scissor];
        }

        // Push constants with viewport offset
        struct
        {
            float screen_w, screen_h, cell_w, cell_h, scroll_offset_px, viewport_x, viewport_y;
        } push_data = {
            (float)pixel_w_, (float)pixel_h_,
            (float)cell_w_, (float)cell_h_,
            pane.scroll_offset_px,
            (float)desc.pixel_x,
            (float)desc.pixel_y
        };

        // Background pass
        [encoder setRenderPipelineState:bg_pipeline_.get()];
        [encoder setVertexBuffer:gridBuf offset:pane_offset_bytes atIndex:0];
        [encoder setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:bg_instances];

        // Foreground pass
        [encoder setRenderPipelineState:fg_pipeline_.get()];
        [encoder setVertexBuffer:gridBuf offset:pane_offset_bytes atIndex:0];
        [encoder setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
        [encoder setFragmentTexture:atlasTex atIndex:0];
        [encoder setFragmentSamplerState:sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:fg_instances];
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
        MetalRenderContext ctx(cmdBuf, encoder, pixel_w_, pixel_h_);
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
}

void MetalRenderer::register_render_pass(std::shared_ptr<IRenderPass> pass)
{
    render_pass_ = std::move(pass);
}

void MetalRenderer::unregister_render_pass()
{
    render_pass_.reset();
}

// ---------------------------------------------------------------------------
// Multi-pane API
// ---------------------------------------------------------------------------

int MetalRenderer::alloc_pane()
{
    // Look for an inactive slot first
    for (int i = 0; i < static_cast<int>(panes_.size()); ++i)
    {
        if (!panes_[static_cast<size_t>(i)].active)
        {
            panes_[static_cast<size_t>(i)] = PaneEntry{};
            panes_[static_cast<size_t>(i)].active = true;
            return i;
        }
    }
    panes_.emplace_back();
    return static_cast<int>(panes_.size()) - 1;
}

void MetalRenderer::free_pane(int pane_id)
{
    if (pane_id == 0)
        return; // Pane 0 is always active
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;
    panes_[static_cast<size_t>(pane_id)].active = false;
}

void MetalRenderer::set_pane_viewport(int pane_id, const PaneDescriptor& desc)
{
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;
    panes_[static_cast<size_t>(pane_id)].descriptor = desc;
}

void MetalRenderer::set_grid_size(int pane_id, int cols, int rows)
{
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;

    panes_[static_cast<size_t>(pane_id)].state.set_grid_size(cols, rows, padding_);

    // Resize the shared GPU buffer to accommodate all panes
    const size_t total_cells = compute_total_buffer_cells();
    const size_t required = total_cells * sizeof(GpuCell);

    id<MTLBuffer> existing = grid_buffer_.get();
    if (!existing || [existing length] < required)
    {
        grid_buffer_.reset([device_.get() newBufferWithLength:required
                                                      options:MTLResourceStorageModeShared]);
    }

    upload_dirty_state();
}

void MetalRenderer::update_cells(int pane_id, std::span<const CellUpdate> updates)
{
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;
    panes_[static_cast<size_t>(pane_id)].state.update_cells(updates);
    upload_dirty_state();
}

void MetalRenderer::set_overlay_cells(int pane_id, std::span<const CellUpdate> updates)
{
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;
    panes_[static_cast<size_t>(pane_id)].state.set_overlay_cells(updates);
    upload_dirty_state();
}

void MetalRenderer::set_cursor(int pane_id, int col, int row, const CursorStyle& style)
{
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;
    panes_[static_cast<size_t>(pane_id)].state.set_cursor(col, row, style);
}

void MetalRenderer::set_default_background(int pane_id, Color bg)
{
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;
    panes_[static_cast<size_t>(pane_id)].bg_color = bg;
    panes_[static_cast<size_t>(pane_id)].state.set_default_background(bg);
}

void MetalRenderer::set_scroll_offset(int pane_id, float px)
{
    if (pane_id < 0 || pane_id >= static_cast<int>(panes_.size()))
        return;
    panes_[static_cast<size_t>(pane_id)].scroll_offset_px = px;
}

} // namespace draxul
