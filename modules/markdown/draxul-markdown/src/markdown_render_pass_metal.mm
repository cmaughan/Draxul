#include <draxul/markdown/markdown_render_pass.h>

#include <draxul/log.h>
#include <draxul/metal/metal_render_context.h>
#include <draxul/metal/objc_ref.h>
#include <draxul/runtime_path.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul::markdown
{
namespace
{

struct PushConstants
{
    float viewport[4] = {};
};

bool valid_upload_rect(const MarkdownAtlasUpload& upload)
{
    return upload.atlas_width > 0 && upload.atlas_height > 0
        && upload.rect.pos.x >= 0 && upload.rect.pos.y >= 0
        && upload.rect.size.x > 0 && upload.rect.size.y > 0
        && upload.rect.pos.x + upload.rect.size.x <= upload.atlas_width
        && upload.rect.pos.y + upload.rect.size.y <= upload.atlas_height;
}

template <typename T>
bool instance_range_valid(const std::vector<T>& instances, uint32_t first, uint32_t count)
{
    return first <= instances.size() && count <= instances.size() - first;
}

} // namespace

struct MarkdownRenderPass::State
{
    struct FrameResources
    {
        ObjCRef<id<MTLBuffer>> rect_buffer;
        ObjCRef<id<MTLBuffer>> glyph_buffer;
        ObjCRef<id<MTLBuffer>> atlas_staging_buffer;
        size_t rect_capacity = 0;
        size_t glyph_capacity = 0;
        size_t atlas_staging_capacity = 0;
        uint64_t uploaded_draw_revision = 0;
    };

    struct AtlasTexture
    {
        ObjCRef<id<MTLTexture>> texture;
        uint32_t generation = 0;
        uint64_t upload_revision = 0;
        int width = 0;
        int height = 0;
        bool ready = false;
    };

    struct PreparedAtlasUpload
    {
        const MarkdownAtlasUpload* upload = nullptr;
        size_t bytes_per_row = 0;
        size_t byte_count = 0;
    };

    ObjCRef<id<MTLDevice>> device;
    ObjCRef<id<MTLSamplerState>> sampler;
    ObjCRef<id<MTLRenderPipelineState>> rect_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> glyph_pipeline;
    std::vector<FrameResources> frames;
    std::unordered_map<uint32_t, AtlasTexture> atlases;
    uint32_t buffered_frame_count = 0;

    void shutdown()
    {
        glyph_pipeline.reset();
        rect_pipeline.reset();
        sampler.reset();
        frames.clear();
        atlases.clear();
        device.reset();
        buffered_frame_count = 0;
    }

    bool ensure_device(id<MTLDevice> current_device)
    {
        if (device.get() == current_device)
            return true;
        shutdown();
        device.reset(current_device);
        return device.get() != nil;
    }

    bool ensure_sampler()
    {
        if (sampler)
            return true;

        MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.magFilter = MTLSamplerMinMagFilterNearest;
        desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        id<MTLSamplerState> created = [device.get() newSamplerStateWithDescriptor:desc];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create Metal sampler");
            return false;
        }
        sampler.reset(created);
        return true;
    }

    bool ensure_frame_resources(uint32_t requested_count)
    {
        requested_count = std::max(1u, requested_count);
        if (buffered_frame_count == requested_count && frames.size() == requested_count)
            return true;

        frames.clear();
        frames.resize(requested_count);
        buffered_frame_count = requested_count;
        return true;
    }

    FrameResources& frame_for(const MetalRenderContext& ctx)
    {
        return frames[ctx.frame_index() % frames.size()];
    }

    bool ensure_buffer(ObjCRef<id<MTLBuffer>>& buffer, size_t& capacity, size_t required_size, const char* label)
    {
        if (required_size == 0 || capacity >= required_size)
            return true;

        id<MTLBuffer> created = [device.get() newBufferWithLength:required_size
                                                          options:MTLResourceStorageModeShared];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "Markdown: failed to create Metal %s buffer (%zu bytes)", label, required_size);
            return false;
        }

        buffer.reset(created);
        capacity = required_size;
        return true;
    }

    bool upload_frame_buffers(FrameResources& frame, const MarkdownDrawList& draw_list, uint64_t draw_revision)
    {
        if (frame.uploaded_draw_revision == draw_revision)
            return true;

        const size_t rect_bytes = draw_list.rects.size() * sizeof(MarkdownRectInstance);
        const size_t glyph_bytes = draw_list.glyphs.size() * sizeof(MarkdownGlyphInstance);

        if (!ensure_buffer(frame.rect_buffer, frame.rect_capacity, rect_bytes, "rect instance"))
            return false;
        if (!ensure_buffer(frame.glyph_buffer, frame.glyph_capacity, glyph_bytes, "glyph instance"))
            return false;

        if (rect_bytes > 0)
        {
            void* dst = [frame.rect_buffer.get() contents];
            if (!dst)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: Metal rect buffer has no CPU mapping");
                return false;
            }
            std::memcpy(dst, draw_list.rects.data(), rect_bytes);
        }

        if (glyph_bytes > 0)
        {
            void* dst = [frame.glyph_buffer.get() contents];
            if (!dst)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: Metal glyph buffer has no CPU mapping");
                return false;
            }
            std::memcpy(dst, draw_list.glyphs.data(), glyph_bytes);
        }

        frame.uploaded_draw_revision = draw_revision;
        return true;
    }

    bool ensure_atlas_texture(AtlasTexture& atlas, const MarkdownAtlasUpload& upload)
    {
        const bool recreate = !atlas.texture
            || atlas.width != upload.atlas_width
            || atlas.height != upload.atlas_height
            || atlas.generation != upload.generation;
        if (!recreate)
            return true;

        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:static_cast<NSUInteger>(upload.atlas_width)
                                        height:static_cast<NSUInteger>(upload.atlas_height)
                                     mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        id<MTLTexture> created = [device.get() newTextureWithDescriptor:desc];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create Metal atlas texture");
            return false;
        }

        atlas.texture.reset(created);
        atlas.generation = upload.generation;
        atlas.upload_revision = 0;
        atlas.width = upload.atlas_width;
        atlas.height = upload.atlas_height;
        atlas.ready = false;
        return true;
    }

    bool ensure_atlas_staging_buffer(FrameResources& frame, size_t required_size)
    {
        if (required_size == 0 || frame.atlas_staging_capacity >= required_size)
            return true;

        id<MTLBuffer> created = [device.get() newBufferWithLength:required_size
                                                          options:MTLResourceStorageModeShared];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "Markdown: failed to create Metal atlas staging buffer (%zu bytes)", required_size);
            return false;
        }

        frame.atlas_staging_buffer.reset(created);
        frame.atlas_staging_capacity = required_size;
        return true;
    }

    bool prepare_atlas_upload(const MarkdownAtlasUpload& upload, std::vector<PreparedAtlasUpload>& prepared, size_t& total_bytes)
    {
        if (!valid_upload_rect(upload))
        {
            DRAXUL_LOG_WARN(LogCategory::Renderer,
                "Markdown: ignoring invalid atlas upload id=%u rect=(%d,%d %dx%d) atlas=%dx%d",
                upload.atlas_id,
                upload.rect.pos.x,
                upload.rect.pos.y,
                upload.rect.size.x,
                upload.rect.size.y,
                upload.atlas_width,
                upload.atlas_height);
            return true;
        }

        const size_t bytes_per_row = static_cast<size_t>(upload.rect.size.x) * 4u;
        const size_t required_bytes = bytes_per_row * static_cast<size_t>(upload.rect.size.y);
        if (upload.rgba.size() < required_bytes)
        {
            DRAXUL_LOG_WARN(LogCategory::Renderer,
                "Markdown: ignoring short atlas upload id=%u bytes=%zu required=%zu",
                upload.atlas_id,
                upload.rgba.size(),
                required_bytes);
            return true;
        }

        auto& atlas = atlases[upload.atlas_id];
        if (atlas.texture && atlas.generation == upload.generation
            && atlas.upload_revision >= upload.upload_revision)
        {
            return true;
        }

        if (!ensure_atlas_texture(atlas, upload))
            return false;

        prepared.push_back(PreparedAtlasUpload{ &upload, bytes_per_row, required_bytes });
        total_bytes += required_bytes;
        return true;
    }

    bool apply_atlas_uploads(FrameResources& frame, id<MTLCommandBuffer> command_buffer, const std::vector<MarkdownAtlasUpload>& uploads)
    {
        if (!command_buffer)
            return false;

        atlases.reserve(atlases.size() + uploads.size());
        std::vector<PreparedAtlasUpload> prepared;
        prepared.reserve(uploads.size());
        size_t total_bytes = 0;

        for (const auto& upload : uploads)
        {
            if (!prepare_atlas_upload(upload, prepared, total_bytes))
                return false;
        }
        if (prepared.empty())
            return true;

        if (!ensure_atlas_staging_buffer(frame, total_bytes))
            return false;

        auto* dst = static_cast<uint8_t*>([frame.atlas_staging_buffer.get() contents]);
        if (!dst)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: Metal atlas staging buffer has no CPU mapping");
            return false;
        }

        size_t offset = 0;
        for (const auto& entry : prepared)
        {
            std::memcpy(dst + offset, entry.upload->rgba.data(), entry.byte_count);
            offset += entry.byte_count;
        }

        id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
        if (!blit)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create Metal atlas blit encoder");
            return false;
        }

        offset = 0;
        for (const auto& entry : prepared)
        {
            const auto& upload = *entry.upload;
            auto atlas_it = atlases.find(upload.atlas_id);
            if (atlas_it == atlases.end() || !atlas_it->second.texture)
            {
                offset += entry.byte_count;
                continue;
            }

            [blit copyFromBuffer:frame.atlas_staging_buffer.get()
                       sourceOffset:static_cast<NSUInteger>(offset)
                  sourceBytesPerRow:static_cast<NSUInteger>(entry.bytes_per_row)
                sourceBytesPerImage:static_cast<NSUInteger>(entry.byte_count)
                         sourceSize:MTLSizeMake(static_cast<NSUInteger>(upload.rect.size.x),
                                        static_cast<NSUInteger>(upload.rect.size.y), 1)
                          toTexture:atlas_it->second.texture.get()
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:MTLOriginMake(static_cast<NSUInteger>(upload.rect.pos.x),
                                        static_cast<NSUInteger>(upload.rect.pos.y), 0)];
            atlas_it->second.upload_revision = upload.upload_revision;
            atlas_it->second.ready = true;
            offset += entry.byte_count;
        }
        [blit endEncoding];
        return true;
    }

    bool create_pipeline(
        id<MTLLibrary> library,
        NSString* vertex_name,
        NSString* fragment_name,
        const char* label,
        ObjCRef<id<MTLRenderPipelineState>>& pipeline)
    {
        id<MTLFunction> vertex = [library newFunctionWithName:vertex_name];
        id<MTLFunction> fragment = [library newFunctionWithName:fragment_name];
        if (!vertex || !fragment)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "Markdown: failed to load Metal %s shader functions", label);
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertex;
        desc.fragmentFunction = fragment;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        NSError* error = nil;
        id<MTLRenderPipelineState> created = [device.get() newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to create Metal %s pipeline: %s",
                label,
                error ? [[error localizedDescription] UTF8String] : "unknown error");
            return false;
        }

        pipeline.reset(created);
        return true;
    }

    bool ensure_pipelines()
    {
        if (rect_pipeline && glyph_pipeline)
            return true;

        const auto shader_path = bundled_asset_path("shaders") / "markdown.metallib";
        NSString* path = [NSString stringWithUTF8String:shader_path.string().c_str()];
        NSError* error = nil;
        NSURL* url = [NSURL fileURLWithPath:path];
        id<MTLLibrary> library = [device.get() newLibraryWithURL:url error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Markdown: failed to load markdown.metallib: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown error");
            return false;
        }

        rect_pipeline.reset();
        glyph_pipeline.reset();
        if (!create_pipeline(
                library, @"markdown_rect_vertex", @"markdown_rect_fragment", "rect", rect_pipeline))
        {
            return false;
        }
        if (!create_pipeline(
                library, @"markdown_glyph_vertex", @"markdown_glyph_fragment", "glyph", glyph_pipeline))
        {
            rect_pipeline.reset();
            return false;
        }
        return true;
    }

    AtlasTexture* atlas_for_batch(const MarkdownGlyphBatch& batch)
    {
        auto it = atlases.find(batch.atlas_id);
        if (it == atlases.end() || !it->second.texture || !it->second.ready
            || it->second.generation != batch.atlas_generation)
            return nullptr;
        return &it->second;
    }
};

MarkdownRenderPass::MarkdownRenderPass() = default;

MarkdownRenderPass::~MarkdownRenderPass()
{
    if (state_)
        state_->shutdown();
}

MarkdownRenderPass::MarkdownRenderPass(MarkdownRenderPass&&) noexcept = default;
MarkdownRenderPass& MarkdownRenderPass::operator=(MarkdownRenderPass&& other) noexcept
{
    if (this == &other)
        return *this;

    if (state_)
        state_->shutdown();
    state_ = std::move(other.state_);
    draw_list_ = std::move(other.draw_list_);
    atlas_uploads_ = std::move(other.atlas_uploads_);
    draw_revision_ = other.draw_revision_;
    other.draw_revision_ = 0;
    return *this;
}

void MarkdownRenderPass::record_prepass(IRenderContext& ctx)
{
    if (!draw_list_.has_work() && atlas_uploads_.empty())
        return;

    auto& metal_ctx = static_cast<MetalRenderContext&>(ctx);
    if (!state_)
        state_ = std::make_unique<State>();
    if (!state_->ensure_device(metal_ctx.device()))
        return;

    if (!state_->ensure_frame_resources(metal_ctx.buffered_frame_count()))
        return;

    auto& frame = state_->frame_for(metal_ctx);
    if (!atlas_uploads_.empty() && state_->apply_atlas_uploads(frame, metal_ctx.command_buffer(), atlas_uploads_))
        atlas_uploads_.clear();

    if (!draw_list_.has_work())
        return;

    state_->upload_frame_buffers(frame, draw_list_, draw_revision_);
}

void MarkdownRenderPass::record(IRenderContext& ctx)
{
    if (!state_ || !draw_list_.has_work() || !state_->device.get())
        return;

    auto& metal_ctx = static_cast<MetalRenderContext&>(ctx);
    if (!state_->ensure_frame_resources(metal_ctx.buffered_frame_count()))
        return;
    if (!state_->ensure_pipelines())
        return;

    auto& frame = state_->frame_for(metal_ctx);
    if (!state_->upload_frame_buffers(frame, draw_list_, draw_revision_))
        return;

    PushConstants push;
    push.viewport[0] = static_cast<float>(std::max(1, metal_ctx.viewport_w()));
    push.viewport[1] = static_cast<float>(std::max(1, metal_ctx.viewport_h()));

    id<MTLRenderCommandEncoder> encoder = metal_ctx.encoder();
    if (!draw_list_.rects.empty() && frame.rect_buffer)
    {
        [encoder setRenderPipelineState:state_->rect_pipeline.get()];
        [encoder setVertexBuffer:frame.rect_buffer.get() offset:0 atIndex:0];
        [encoder setVertexBytes:&push length:sizeof(push) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:static_cast<NSUInteger>(draw_list_.rects.size())
                   baseInstance:0];
    }

    if (draw_list_.glyph_batches.empty() || !frame.glyph_buffer)
        return;
    if (!state_->ensure_sampler())
        return;

    [encoder setRenderPipelineState:state_->glyph_pipeline.get()];
    [encoder setVertexBytes:&push length:sizeof(push) atIndex:1];
    [encoder setFragmentSamplerState:state_->sampler.get() atIndex:0];

    for (const auto& batch : draw_list_.glyph_batches)
    {
        if (batch.count == 0 || !instance_range_valid(draw_list_.glyphs, batch.first, batch.count))
            continue;

        State::AtlasTexture* atlas = state_->atlas_for_batch(batch);
        if (!atlas)
            continue;

        [encoder setVertexBuffer:frame.glyph_buffer.get()
                          offset:static_cast<NSUInteger>(batch.first) * sizeof(MarkdownGlyphInstance)
                         atIndex:0];
        [encoder setFragmentTexture:atlas->texture.get() atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:static_cast<NSUInteger>(batch.count)
                   baseInstance:0];
    }
}

} // namespace draxul::markdown
