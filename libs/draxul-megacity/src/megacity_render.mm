#include "isometric_scene_pass.h"

#include "megacity_material_assets.h"
#include "mesh_library.h"
#include "objc_ref.h"
#include "shadow_cascade.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <draxul/log.h>
#include <imgui.h>
#import <simd/simd.h>
#include <vector>

namespace draxul
{

namespace
{

struct FrameUniforms
{
    simd_float4x4 view;
    simd_float4x4 proj;
    simd_float4x4 inv_view_proj;
    simd_float4 camera_pos;
    simd_float4 light_dir;
    simd_float4 point_light_pos;
    simd_float4 label_fade_px;
    simd_float4 render_tuning;
    simd_float4 screen_params; // x = viewport origin x, y = viewport origin y, z = 1 / viewport width, w = 1 / viewport height
    simd_float4 ao_params; // x = radius world, y = radius pixels, z = bias, w = power
    simd_float4 debug_view; // x = AO debug mode, y = AO denoise enabled
    simd_float4 world_debug_bounds; // x = min x, y = max x, z = min z, w = max z
    simd_float4x4 shadow_view_proj[kShadowCascadeCount];
    simd_float4x4 shadow_texture_matrix[kShadowCascadeCount];
    simd_float4 shadow_split_depths; // x/y/z = cascade end depth in clip space [0,1]
    simd_float4 shadow_params; // x = cascade count, y = sample depth bias, z = normal bias, w = 1 / shadow resolution
};

struct ObjectUniforms
{
    simd_float4x4 world;
    simd_float4 color;
    simd_uint4 material_data;
    simd_float4 uv_rect;
    simd_float4 label_metrics;
};

struct MaterialInstanceUniform
{
    simd_float4 scalar_params;
    simd_uint4 texture_indices;
    simd_uint4 metadata;
};

struct MaterialUniforms
{
    std::array<MaterialInstanceUniform, kMaxSceneMaterials> materials;
};

struct MeshBuffers
{
    ObjCRef<id<MTLBuffer>> vertex_buffer;
    ObjCRef<id<MTLBuffer>> index_buffer;
    NSUInteger index_count = 0;
};

struct BufferSlice
{
    id<MTLBuffer> buffer = nil;
    NSUInteger offset = 0;
    void* mapped = nullptr;
};

struct MeshSlice
{
    id<MTLBuffer> vertex_buffer = nil;
    NSUInteger vertex_offset = 0;
    id<MTLBuffer> index_buffer = nil;
    NSUInteger index_offset = 0;
    NSUInteger index_count = 0;
};

struct TransientBufferArena
{
    ObjCRef<id<MTLBuffer>> buffer;
    NSUInteger head = 0;

    void reset()
    {
        head = 0;
    }
};

struct TransientGeometryArena
{
    TransientBufferArena vertices;
    TransientBufferArena indices;

    void reset()
    {
        vertices.reset();
        indices.reset();
    }
};

struct FrameResources
{
    ObjCRef<id<MTLBuffer>> frame_uniforms;
    ObjCRef<id<MTLBuffer>> material_uniforms;
    TransientGeometryArena geometry_arena;
};

bool same_grid_spec(const FloorGridSpec& a, const FloorGridSpec& b)
{
    return a.enabled == b.enabled
        && a.min_x == b.min_x
        && a.max_x == b.max_x
        && a.min_z == b.min_z
        && a.max_z == b.max_z
        && a.tile_size == b.tile_size
        && a.line_width == b.line_width
        && a.y == b.y
        && a.color.x == b.color.x
        && a.color.y == b.color.y
        && a.color.z == b.color.z
        && a.color.w == b.color.w;
}

simd_float4x4 to_simd_matrix(const glm::mat4& mat)
{
    simd_float4x4 out;
    for (int column = 0; column < 4; ++column)
    {
        out.columns[column] = simd_make_float4(
            mat[column][0],
            mat[column][1],
            mat[column][2],
            mat[column][3]);
    }
    return out;
}

MaterialUniforms build_material_uniforms(const SceneSnapshot& scene)
{
    MaterialUniforms uniforms{};
    const size_t material_count = std::min(scene.materials.size(), uniforms.materials.size());
    for (size_t index = 0; index < material_count; ++index)
    {
        const SceneMaterial& material = scene.materials[index];
        uniforms.materials[index].scalar_params = simd_make_float4(
            material.scalar_params.x,
            material.scalar_params.y,
            material.scalar_params.z,
            material.scalar_params.w);
        uniforms.materials[index].texture_indices = simd_make_uint4(
            material.texture_indices.x,
            material.texture_indices.y,
            material.texture_indices.z,
            material.texture_indices.w);
        uniforms.materials[index].metadata = simd_make_uint4(
            static_cast<uint32_t>(material.shading_model),
            material.metadata.y,
            material.metadata.z,
            material.metadata.w);
    }
    return uniforms;
}

float compute_ao_radius_pixels(const glm::mat4& proj, float radius_world, int viewport_h)
{
    if (viewport_h <= 0)
        return 1.0f;
    const float ortho_scale_y = std::abs(proj[1][1]);
    return std::max(1.0f, radius_world * ortho_scale_y * 0.5f * static_cast<float>(viewport_h));
}

NSUInteger mip_level_count_for_size(int width, int height)
{
    const int max_dim = std::max(width, height);
    if (max_dim <= 0)
        return 1;
    return static_cast<NSUInteger>(std::floor(std::log2(static_cast<double>(max_dim)))) + 1;
}

bool upload_mesh(id<MTLDevice> device, const MeshData& mesh, MeshBuffers& buffers)
{
    id<MTLBuffer> vertex_buffer = [device newBufferWithBytes:mesh.vertices.data()
                                                      length:mesh.vertices.size() * sizeof(SceneVertex)
                                                     options:MTLResourceStorageModeShared];
    id<MTLBuffer> index_buffer = [device newBufferWithBytes:mesh.indices.data()
                                                     length:mesh.indices.size() * sizeof(uint16_t)
                                                    options:MTLResourceStorageModeShared];
    if (!vertex_buffer || !index_buffer)
        return false;

    buffers.vertex_buffer.reset(vertex_buffer);
    buffers.index_buffer.reset(index_buffer);
    buffers.index_count = static_cast<NSUInteger>(mesh.indices.size());
    return true;
}

NSUInteger align_up(NSUInteger value, NSUInteger alignment)
{
    if (alignment <= 1)
        return value;
    const NSUInteger remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

NSUInteger grow_capacity(NSUInteger current_size, NSUInteger required_size)
{
    if (current_size == 0)
        return required_size;
    return std::max(required_size, current_size * 2);
}

bool ensure_buffer_capacity(id<MTLDevice> device, NSUInteger required_size, ObjCRef<id<MTLBuffer>>& buffer)
{
    if (required_size == 0)
        return true;
    if (buffer && [buffer.get() length] >= required_size)
        return true;

    id<MTLBuffer> replacement = [device newBufferWithLength:grow_capacity(buffer ? [buffer.get() length] : 0, required_size)
                                                    options:MTLResourceStorageModeShared];
    if (!replacement)
        return false;

    buffer.reset(replacement);
    return true;
}

bool reserve_transient_buffer(id<MTLDevice> device, TransientBufferArena& arena,
    NSUInteger size, NSUInteger alignment, NSUInteger minimum_size, BufferSlice& slice)
{
    if (size == 0)
    {
        slice = {};
        return true;
    }

    const NSUInteger offset = align_up(arena.head, alignment);
    const NSUInteger required_size = offset + size;
    if (!ensure_buffer_capacity(device, std::max(required_size, minimum_size), arena.buffer))
        return false;

    slice.buffer = arena.buffer.get();
    slice.offset = offset;
    slice.mapped = static_cast<char*>([arena.buffer.get() contents]) + offset;
    arena.head = required_size;
    return true;
}

bool stream_transient_mesh(id<MTLDevice> device, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice)
{
    constexpr NSUInteger kMinimumVertexArenaBytes = 16 * 1024;
    constexpr NSUInteger kMinimumIndexArenaBytes = 4 * 1024;

    const NSUInteger vertex_bytes = static_cast<NSUInteger>(mesh.vertices.size() * sizeof(SceneVertex));
    const NSUInteger index_bytes = static_cast<NSUInteger>(mesh.indices.size() * sizeof(uint16_t));
    if (vertex_bytes == 0 || index_bytes == 0)
    {
        slice = {};
        return true;
    }

    BufferSlice vertex_slice;
    if (!reserve_transient_buffer(device, arena.vertices, vertex_bytes, alignof(SceneVertex),
            kMinimumVertexArenaBytes, vertex_slice))
    {
        return false;
    }

    BufferSlice index_slice;
    if (!reserve_transient_buffer(device, arena.indices, index_bytes, alignof(uint16_t),
            kMinimumIndexArenaBytes, index_slice))
    {
        return false;
    }

    std::memcpy(vertex_slice.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(index_slice.mapped, mesh.indices.data(), index_bytes);
    slice.vertex_buffer = vertex_slice.buffer;
    slice.vertex_offset = vertex_slice.offset;
    slice.index_buffer = index_slice.buffer;
    slice.index_offset = index_slice.offset;
    slice.index_count = static_cast<NSUInteger>(mesh.indices.size());
    return true;
}

} // namespace

struct GBufferTargets
{
    ObjCRef<id<MTLTexture>> normal; // RGBA8Unorm — RG octahedral normal, BA reserved
    ObjCRef<id<MTLTexture>> ao_raw; // RGBA8Unorm — raw ambient occlusion before denoise
    ObjCRef<id<MTLTexture>> ao; // RGBA8Unorm — R ambient occlusion, GBA reserved
    ObjCRef<id<MTLTexture>> depth; // Depth32Float — hardware depth
    std::array<ObjCRef<id<MTLTexture>>, kShadowCascadeCount> shadow_maps; // Depth32Float directional shadow cascades
    ObjCRef<id<MTLTexture>> scene_color_msaa; // RGBA16Float MSAA scene color
    ObjCRef<id<MTLTexture>> scene_depth_msaa; // Depth32Float MSAA scene depth
    ObjCRef<id<MTLTexture>> scene_hdr; // RGBA16Float resolved HDR scene
    ObjCRef<id<MTLTexture>> scene_final_srgb; // BGRA8Unorm_sRGB encoded scene
    ObjCRef<id<MTLTexture>> scene_final_unorm; // BGRA8Unorm alias view for present/debug
    int width = 0;
    int height = 0;
};

struct IsometricScenePass::State
{
    ObjCRef<id<MTLRenderPipelineState>> pipeline;
    ObjCRef<id<MTLRenderPipelineState>> debug_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> post_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> present_pipeline;
    ObjCRef<id<MTLDepthStencilState>> depth_state;
    ObjCRef<id<MTLDepthStencilState>> depth_state_no_write;
    MeshBuffers cube_mesh;
    MeshBuffers floor_mesh;
    MeshBuffers tree_bark_mesh;
    MeshBuffers tree_leaf_mesh;
    MeshBuffers road_surface_mesh;
    MeshBuffers roof_sign_mesh;
    MeshBuffers wall_sign_mesh;
    const MeshData* tree_bark_mesh_source = nullptr;
    const MeshData* tree_leaf_mesh_source = nullptr;
    std::vector<MeshBuffers> custom_meshes;
    std::vector<const MeshData*> custom_mesh_sources;
    MeshData cached_grid_mesh;
    FloorGridSpec cached_grid_spec;
    bool has_cached_grid_mesh = false;
    ObjCRef<id<MTLTexture>> label_atlas_texture;
    ObjCRef<id<MTLSamplerState>> label_sampler;
    uint64_t label_atlas_revision = 0;
    std::vector<FrameResources> frame_resources;
    uint32_t buffered_frame_count = 1;
    bool initialized = false;

    // GBuffer pre-pass resources
    ObjCRef<id<MTLRenderPipelineState>> shadow_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> gbuffer_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> ao_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> ao_blur_pipeline;
    ObjCRef<id<MTLSamplerState>> gbuffer_sampler;
    ObjCRef<id<MTLSamplerState>> gbuffer_point_sampler;
    ObjCRef<id<MTLSamplerState>> material_sampler;
    std::array<ObjCRef<id<MTLTexture>>, kSceneMaterialTextureCount> material_textures;
    std::vector<GBufferTargets> gbuffer_targets; // per frame
    bool gbuffer_initialized = false;
    uint32_t last_prepass_frame = 0;
    NSUInteger scene_sample_count = 4;
    int shadow_map_resolution = 2048;

    // Tooltip overlay resources
    ObjCRef<id<MTLRenderPipelineState>> tooltip_pipeline;
    ObjCRef<id<MTLTexture>> tooltip_texture;
    ObjCRef<id<MTLSamplerState>> tooltip_sampler;
    uint64_t tooltip_texture_revision = 0;

    bool init(id<MTLDevice> device, int grid_width, int grid_height, float tile_size)
    {
        if (initialized)
            return pipeline.get() != nil && debug_pipeline.get() != nil
                && post_pipeline.get() != nil && present_pipeline.get() != nil
                && depth_state.get() != nil;

        initialized = true;
        NSError* error = nil;
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* libPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_scene.metallib"];
        NSURL* libURL = [NSURL fileURLWithPath:libPath];

        id<MTLLibrary> library = [device newLibraryWithURL:libURL error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_scene.metallib from %s: %s",
                [libPath UTF8String],
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> vert_fn = [library newFunctionWithName:@"scene_vertex"];
        id<MTLFunction> frag_fn = [library newFunctionWithName:@"scene_fragment"];
        if (!vert_fn || !frag_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: scene_vertex or scene_fragment not found in shader library");
            return false;
        }

        MTLVertexDescriptor* vertex_desc = [[MTLVertexDescriptor alloc] init];
        vertex_desc.attributes[0].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[0].offset = offsetof(SceneVertex, position);
        vertex_desc.attributes[0].bufferIndex = 0;
        vertex_desc.attributes[1].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[1].offset = offsetof(SceneVertex, normal);
        vertex_desc.attributes[1].bufferIndex = 0;
        vertex_desc.attributes[2].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[2].offset = offsetof(SceneVertex, color);
        vertex_desc.attributes[2].bufferIndex = 0;
        vertex_desc.attributes[3].format = MTLVertexFormatFloat2;
        vertex_desc.attributes[3].offset = offsetof(SceneVertex, uv);
        vertex_desc.attributes[3].bufferIndex = 0;
        vertex_desc.attributes[4].format = MTLVertexFormatFloat;
        vertex_desc.attributes[4].offset = offsetof(SceneVertex, tex_blend);
        vertex_desc.attributes[4].bufferIndex = 0;
        vertex_desc.attributes[5].format = MTLVertexFormatFloat4;
        vertex_desc.attributes[5].offset = offsetof(SceneVertex, tangent);
        vertex_desc.attributes[5].bufferIndex = 0;
        vertex_desc.layouts[0].stride = sizeof(SceneVertex);
        vertex_desc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vert_fn;
        desc.fragmentFunction = frag_fn;
        desc.vertexDescriptor = vertex_desc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        desc.rasterSampleCount = scene_sample_count;

        id<MTLRenderPipelineState> pipeline_state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create scene pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        pipeline.reset(pipeline_state);

        id<MTLFunction> debug_frag_fn = [library newFunctionWithName:@"debug_fragment"];
        if (debug_frag_fn)
        {
            desc.fragmentFunction = debug_frag_fn;
            id<MTLRenderPipelineState> debug_pipeline_state = [device newRenderPipelineStateWithDescriptor:desc
                                                                                                     error:&error];
            if (debug_pipeline_state)
                debug_pipeline.reset(debug_pipeline_state);
            else
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "MegaCity: failed to create debug pipeline: %s",
                    error ? [[error localizedDescription] UTF8String] : "unknown");
        }
        else
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: debug_fragment not found in shader library");
        }

        id<MTLFunction> fullscreen_vert_fn = [library newFunctionWithName:@"fullscreen_vertex"];
        id<MTLFunction> post_frag_fn = [library newFunctionWithName:@"scene_post_fragment"];
        id<MTLFunction> present_frag_fn = [library newFunctionWithName:@"scene_present_fragment"];
        if (!fullscreen_vert_fn || !post_frag_fn || !present_frag_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: fullscreen/post/present shader functions not found in scene library");
            return false;
        }

        MTLRenderPipelineDescriptor* post_desc = [[MTLRenderPipelineDescriptor alloc] init];
        post_desc.vertexFunction = fullscreen_vert_fn;
        post_desc.fragmentFunction = post_frag_fn;
        post_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;

        id<MTLRenderPipelineState> post_pipeline_state = [device newRenderPipelineStateWithDescriptor:post_desc
                                                                                                error:&error];
        if (!post_pipeline_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create scene post pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        post_pipeline.reset(post_pipeline_state);

        MTLRenderPipelineDescriptor* present_desc = [[MTLRenderPipelineDescriptor alloc] init];
        present_desc.vertexFunction = fullscreen_vert_fn;
        present_desc.fragmentFunction = present_frag_fn;
        present_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        id<MTLRenderPipelineState> present_pipeline_state = [device newRenderPipelineStateWithDescriptor:present_desc
                                                                                                   error:&error];
        if (!present_pipeline_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create scene present pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        present_pipeline.reset(present_pipeline_state);

        MTLDepthStencilDescriptor* depth_desc = [[MTLDepthStencilDescriptor alloc] init];
        depth_desc.depthCompareFunction = MTLCompareFunctionLessEqual;
        depth_desc.depthWriteEnabled = YES;
        depth_state.reset([device newDepthStencilStateWithDescriptor:depth_desc]);
        if (!depth_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create depth state");
            return false;
        }

        depth_desc.depthWriteEnabled = NO;
        depth_state_no_write.reset([device newDepthStencilStateWithDescriptor:depth_desc]);

        if (!upload_mesh(device, build_unit_cube_mesh(), cube_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload cube mesh");
            return false;
        }
        if (!upload_mesh(device, build_floor_box_mesh(), floor_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload floor mesh");
            return false;
        }
        if (!upload_mesh(device, build_tree_bark_mesh(), tree_bark_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload tree bark mesh");
            return false;
        }
        if (!upload_mesh(device, build_tree_leaf_mesh(), tree_leaf_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload tree leaf mesh");
            return false;
        }
        if (!upload_mesh(device, build_road_surface_mesh(), road_surface_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload road surface mesh");
            return false;
        }
        if (!upload_mesh(device, build_roof_sign_mesh(), roof_sign_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload roof sign mesh");
            return false;
        }
        if (!upload_mesh(device, build_wall_sign_mesh(), wall_sign_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload wall sign mesh");
            return false;
        }

        MTLSamplerDescriptor* sampler_desc = [[MTLSamplerDescriptor alloc] init];
        sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_desc.maxAnisotropy = 8;
        label_sampler.reset([device newSamplerStateWithDescriptor:sampler_desc]);
        if (!label_sampler)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create label sampler");
            return false;
        }

        // Tooltip overlay pipeline (alpha-blended screen-space quad).
        id<MTLFunction> tooltip_vert_fn = [library newFunctionWithName:@"tooltip_vertex"];
        id<MTLFunction> tooltip_frag_fn = [library newFunctionWithName:@"tooltip_fragment"];
        if (tooltip_vert_fn && tooltip_frag_fn)
        {
            MTLRenderPipelineDescriptor* tooltip_desc = [[MTLRenderPipelineDescriptor alloc] init];
            tooltip_desc.vertexFunction = tooltip_vert_fn;
            tooltip_desc.fragmentFunction = tooltip_frag_fn;
            tooltip_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            tooltip_desc.colorAttachments[0].blendingEnabled = YES;
            tooltip_desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
            tooltip_desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
            tooltip_desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            tooltip_desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            tooltip_desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
            tooltip_desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

            id<MTLRenderPipelineState> tooltip_ps = [device newRenderPipelineStateWithDescriptor:tooltip_desc
                                                                                           error:&error];
            if (tooltip_ps)
                tooltip_pipeline.reset(tooltip_ps);
            else
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create tooltip pipeline: %s",
                    error ? [[error localizedDescription] UTF8String] : "unknown");

            // Tooltip sampler (nearest for pixel-precise text).
            MTLSamplerDescriptor* tooltip_sampler_desc = [[MTLSamplerDescriptor alloc] init];
            tooltip_sampler_desc.minFilter = MTLSamplerMinMagFilterNearest;
            tooltip_sampler_desc.magFilter = MTLSamplerMinMagFilterNearest;
            tooltip_sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            tooltip_sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            tooltip_sampler.reset([device newSamplerStateWithDescriptor:tooltip_sampler_desc]);
        }
        else
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: tooltip shader functions not found in scene library");
        }

        DRAXUL_LOG_INFO(LogCategory::App, "MegaCity: scene pipeline initialized");
        return true;
    }

    bool ensure_frame_resources(id<MTLDevice> device, uint32_t frame_count)
    {
        frame_count = std::max(1u, frame_count);
        if (buffered_frame_count == frame_count && frame_resources.size() == frame_count)
            return true;

        frame_resources.clear();
        frame_resources.resize(frame_count);
        buffered_frame_count = frame_count;

        for (auto& frame : frame_resources)
        {
            if (!ensure_buffer_capacity(device, sizeof(FrameUniforms), frame.frame_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to allocate frame uniform buffer");
                frame_resources.clear();
                buffered_frame_count = 1;
                return false;
            }
            if (!ensure_buffer_capacity(device, sizeof(MaterialUniforms), frame.material_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to allocate material uniform buffer");
                frame_resources.clear();
                buffered_frame_count = 1;
                return false;
            }
        }
        return true;
    }

    bool ensure_tree_mesh(
        id<MTLDevice> device,
        const std::shared_ptr<const MeshData>& tree_bark_mesh_data,
        const std::shared_ptr<const MeshData>& tree_leaf_mesh_data)
    {
        if (tree_bark_mesh_data
            && !(tree_bark_mesh_source == tree_bark_mesh_data.get() && tree_bark_mesh.index_count > 0))
        {
            MeshBuffers replacement;
            if (!upload_mesh(device, *tree_bark_mesh_data, replacement))
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload procedural tree bark mesh");
                return false;
            }
            tree_bark_mesh = std::move(replacement);
            tree_bark_mesh_source = tree_bark_mesh_data.get();
        }
        if (tree_leaf_mesh_data
            && !(tree_leaf_mesh_source == tree_leaf_mesh_data.get() && tree_leaf_mesh.index_count > 0))
        {
            MeshBuffers replacement;
            if (!upload_mesh(device, *tree_leaf_mesh_data, replacement))
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload procedural tree leaf mesh");
                return false;
            }
            tree_leaf_mesh = std::move(replacement);
            tree_leaf_mesh_source = tree_leaf_mesh_data.get();
        }
        return true;
    }

    bool ensure_custom_meshes(
        id<MTLDevice> device,
        const std::vector<std::shared_ptr<const MeshData>>& custom_mesh_data)
    {
        custom_meshes.resize(custom_mesh_data.size());
        custom_mesh_sources.resize(custom_mesh_data.size(), nullptr);
        for (size_t index = 0; index < custom_mesh_data.size(); ++index)
        {
            const auto& mesh_data = custom_mesh_data[index];
            if (!mesh_data)
                continue;
            if (custom_mesh_sources[index] == mesh_data.get() && custom_meshes[index].index_count > 0)
                continue;

            MeshBuffers replacement;
            if (!upload_mesh(device, *mesh_data, replacement))
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload procedural custom mesh");
                return false;
            }
            custom_meshes[index] = std::move(replacement);
            custom_mesh_sources[index] = mesh_data.get();
        }
        return true;
    }

    bool ensure_floor_grid(id<MTLDevice> device, const FloorGridSpec& spec)
    {
        (void)device;
        if (!spec.enabled)
        {
            cached_grid_mesh = {};
            cached_grid_spec = {};
            has_cached_grid_mesh = false;
            return true;
        }
        if (has_cached_grid_mesh && same_grid_spec(cached_grid_spec, spec))
            return true;

        cached_grid_mesh = build_outline_grid_mesh(spec);
        cached_grid_spec = spec;
        has_cached_grid_mesh = true;
        return true;
    }

    bool ensure_label_atlas(id<MTLDevice> device, const std::shared_ptr<const LabelAtlasData>& atlas)
    {
        if (!atlas || !atlas->valid())
        {
            if (label_atlas_texture)
                return true;

            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                            width:1
                                                                                           height:1
                                                                                        mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
            if (!texture)
                return false;
            label_atlas_texture.reset(texture);
            const uint8_t clear[4] = { 0, 0, 0, 0 };
            [label_atlas_texture.get() replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                                         mipmapLevel:0
                                           withBytes:clear
                                         bytesPerRow:4];
            label_atlas_revision = 0;
            return true;
        }

        if (label_atlas_revision == atlas->revision && label_atlas_texture)
            return true;

        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                        width:static_cast<NSUInteger>(atlas->width)
                                                                                       height:static_cast<NSUInteger>(atlas->height)
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        if (!texture)
            return false;

        label_atlas_texture.reset(texture);
        [label_atlas_texture.get() replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(atlas->width),
                                                     static_cast<NSUInteger>(atlas->height))
                                     mipmapLevel:0
                                       withBytes:atlas->rgba.data()
                                     bytesPerRow:static_cast<NSUInteger>(atlas->width * 4)];
        label_atlas_revision = atlas->revision;
        return true;
    }

    id<MTLTexture> make_texture(id<MTLCommandBuffer> cmd_buf, MTLPixelFormat format, const LoadedTextureImage& image)
    {
        id<MTLDevice> device = cmd_buf.device;
        const NSUInteger mip_count = mip_level_count_for_size(image.width, image.height);
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                        width:static_cast<NSUInteger>(image.width)
                                                                                       height:static_cast<NSUInteger>(image.height)
                                                                                    mipmapped:(mip_count > 1)];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        if (!texture)
            return nil;

        [texture replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(image.width), static_cast<NSUInteger>(image.height))
                   mipmapLevel:0
                     withBytes:image.rgba.data()
                   bytesPerRow:static_cast<NSUInteger>(image.width * 4)];
        if (mip_count > 1)
        {
            id<MTLCommandQueue> command_queue = [cmd_buf commandQueue];
            if (!command_queue)
                return nil;
            id<MTLCommandBuffer> mip_cmd = [command_queue commandBuffer];
            if (!mip_cmd)
                return nil;
            id<MTLBlitCommandEncoder> blit = [mip_cmd blitCommandEncoder];
            if (!blit)
                return nil;
            [blit generateMipmapsForTexture:texture];
            [blit endEncoding];
            [mip_cmd commit];
            [mip_cmd waitUntilCompleted];
        }
        return texture;
    }

    id<MTLTexture> make_solid_texture(id<MTLCommandBuffer> cmd_buf, MTLPixelFormat format, std::array<uint8_t, 4> rgba)
    {
        LoadedTextureImage image;
        image.width = 1;
        image.height = 1;
        image.rgba.assign(rgba.begin(), rgba.end());
        return make_texture(cmd_buf, format, image);
    }

    bool ensure_road_materials(id<MTLCommandBuffer> cmd_buf)
    {
        if (std::all_of(material_textures.begin(), material_textures.end(),
                [](const ObjCRef<id<MTLTexture>>& texture) { return texture.get() != nil; })
            && material_sampler)
            return true;

        const AsphaltRoadMaterialImages road_images = load_asphalt_road_material_images();
        const PavingSidewalkMaterialImages sidewalk_images = load_paving_sidewalk_material_images();
        const WoodBuildingMaterialImages wood_images = load_wood_building_material_images();
        const BarkTreeMaterialImages bark_images = load_bark_tree_material_images();
        const LeafAtlasMaterialImages leaf_images = load_leaf_atlas_material_images();
        if (!road_images.valid() || !sidewalk_images.valid() || !wood_images.valid()
            || !bark_images.valid() || !leaf_images.valid())
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to load Megacity material images");
            return false;
        }

        material_textures[static_cast<size_t>(SceneTextureId::FallbackAlbedoSrgb)].reset(
            make_solid_texture(cmd_buf, MTLPixelFormatRGBA8Unorm_sRGB, { 255, 255, 255, 255 }));
        material_textures[static_cast<size_t>(SceneTextureId::FallbackScalar)].reset(
            make_solid_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, { 255, 255, 255, 255 }));
        material_textures[static_cast<size_t>(SceneTextureId::FallbackNormal)].reset(
            make_solid_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, { 128, 128, 255, 255 }));
        material_textures[static_cast<size_t>(SceneTextureId::AsphaltAlbedo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm_sRGB, road_images.albedo));
        material_textures[static_cast<size_t>(SceneTextureId::AsphaltNormal)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, road_images.normal));
        material_textures[static_cast<size_t>(SceneTextureId::AsphaltRoughness)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, road_images.roughness));
        material_textures[static_cast<size_t>(SceneTextureId::AsphaltAo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, road_images.ao));
        material_textures[static_cast<size_t>(SceneTextureId::SidewalkAlbedo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm_sRGB, sidewalk_images.albedo));
        material_textures[static_cast<size_t>(SceneTextureId::SidewalkNormal)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, sidewalk_images.normal));
        material_textures[static_cast<size_t>(SceneTextureId::SidewalkRoughness)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, sidewalk_images.roughness));
        material_textures[static_cast<size_t>(SceneTextureId::SidewalkAo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, sidewalk_images.ao));
        material_textures[static_cast<size_t>(SceneTextureId::WoodAlbedo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm_sRGB, wood_images.albedo));
        material_textures[static_cast<size_t>(SceneTextureId::WoodNormal)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, wood_images.normal));
        material_textures[static_cast<size_t>(SceneTextureId::WoodRoughness)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, wood_images.roughness));
        material_textures[static_cast<size_t>(SceneTextureId::WoodMetalness)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, wood_images.metalness));
        material_textures[static_cast<size_t>(SceneTextureId::WoodAo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, wood_images.ao));
        material_textures[static_cast<size_t>(SceneTextureId::BarkAlbedo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm_sRGB, bark_images.albedo));
        material_textures[static_cast<size_t>(SceneTextureId::BarkNormal)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, bark_images.normal));
        material_textures[static_cast<size_t>(SceneTextureId::BarkRoughness)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, bark_images.roughness));
        material_textures[static_cast<size_t>(SceneTextureId::BarkAo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, bark_images.ao));
        material_textures[static_cast<size_t>(SceneTextureId::LeafAlbedo)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm_sRGB, leaf_images.albedo));
        material_textures[static_cast<size_t>(SceneTextureId::LeafNormal)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, leaf_images.normal));
        material_textures[static_cast<size_t>(SceneTextureId::LeafRoughness)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, leaf_images.roughness));
        material_textures[static_cast<size_t>(SceneTextureId::LeafOpacity)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, leaf_images.opacity));
        material_textures[static_cast<size_t>(SceneTextureId::LeafScattering)].reset(
            make_texture(cmd_buf, MTLPixelFormatRGBA8Unorm, leaf_images.scattering));
        if (!std::all_of(material_textures.begin(), material_textures.end(),
                [](const ObjCRef<id<MTLTexture>>& texture) { return texture.get() != nil; }))
            return false;

        MTLSamplerDescriptor* material_sampler_desc = [[MTLSamplerDescriptor alloc] init];
        material_sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        material_sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        material_sampler_desc.mipFilter = MTLSamplerMipFilterLinear;
        material_sampler_desc.sAddressMode = MTLSamplerAddressModeRepeat;
        material_sampler_desc.tAddressMode = MTLSamplerAddressModeRepeat;
        material_sampler_desc.rAddressMode = MTLSamplerAddressModeRepeat;
        material_sampler_desc.maxAnisotropy = 8;
        material_sampler.reset([cmd_buf.device newSamplerStateWithDescriptor:material_sampler_desc]);
        return material_sampler.get() != nil;
    }

    bool init_gbuffer(id<MTLDevice> device)
    {
        if (gbuffer_initialized)
            return shadow_pipeline.get() != nil && gbuffer_pipeline.get() != nil
                && ao_pipeline.get() != nil && ao_blur_pipeline.get() != nil
                && gbuffer_sampler.get() != nil && gbuffer_point_sampler.get() != nil;

        gbuffer_initialized = true;

        NSError* error = nil;
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* libPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_gbuffer.metallib"];
        NSURL* libURL = [NSURL fileURLWithPath:libPath];

        id<MTLLibrary> library = [device newLibraryWithURL:libURL error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_gbuffer.metallib: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> vert_fn = [library newFunctionWithName:@"gbuffer_vertex"];
        id<MTLFunction> frag_fn = [library newFunctionWithName:@"gbuffer_fragment"];
        id<MTLFunction> shadow_vert_fn = [library newFunctionWithName:@"shadow_vertex"];
        if (!vert_fn || !frag_fn || !shadow_vert_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: gbuffer/shadow shader functions not found");
            return false;
        }

        MTLVertexDescriptor* vertex_desc = [[MTLVertexDescriptor alloc] init];
        vertex_desc.attributes[0].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[0].offset = offsetof(SceneVertex, position);
        vertex_desc.attributes[0].bufferIndex = 0;
        vertex_desc.attributes[1].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[1].offset = offsetof(SceneVertex, normal);
        vertex_desc.attributes[1].bufferIndex = 0;
        vertex_desc.attributes[2].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[2].offset = offsetof(SceneVertex, color);
        vertex_desc.attributes[2].bufferIndex = 0;
        vertex_desc.attributes[3].format = MTLVertexFormatFloat2;
        vertex_desc.attributes[3].offset = offsetof(SceneVertex, uv);
        vertex_desc.attributes[3].bufferIndex = 0;
        vertex_desc.attributes[4].format = MTLVertexFormatFloat;
        vertex_desc.attributes[4].offset = offsetof(SceneVertex, tex_blend);
        vertex_desc.attributes[4].bufferIndex = 0;
        vertex_desc.attributes[5].format = MTLVertexFormatFloat4;
        vertex_desc.attributes[5].offset = offsetof(SceneVertex, tangent);
        vertex_desc.attributes[5].bufferIndex = 0;
        vertex_desc.layouts[0].stride = sizeof(SceneVertex);
        vertex_desc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vert_fn;
        desc.fragmentFunction = frag_fn;
        desc.vertexDescriptor = vertex_desc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pso)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create GBuffer pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        gbuffer_pipeline.reset(pso);

        MTLRenderPipelineDescriptor* shadow_desc = [[MTLRenderPipelineDescriptor alloc] init];
        shadow_desc.vertexFunction = shadow_vert_fn;
        shadow_desc.fragmentFunction = nil;
        shadow_desc.vertexDescriptor = vertex_desc;
        shadow_desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        id<MTLRenderPipelineState> shadow_pso = [device newRenderPipelineStateWithDescriptor:shadow_desc error:&error];
        if (!shadow_pso)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create shadow pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        shadow_pipeline.reset(shadow_pso);

        NSString* aoLibPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_ao.metallib"];
        NSURL* aoLibURL = [NSURL fileURLWithPath:aoLibPath];
        id<MTLLibrary> aoLibrary = [device newLibraryWithURL:aoLibURL error:&error];
        if (!aoLibrary)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_ao.metallib: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> ao_vert_fn = [aoLibrary newFunctionWithName:@"ao_vertex"];
        id<MTLFunction> ao_frag_fn = [aoLibrary newFunctionWithName:@"ao_fragment"];
        id<MTLFunction> ao_blur_frag_fn = [aoLibrary newFunctionWithName:@"ao_blur_fragment"];
        if (!ao_vert_fn || !ao_frag_fn || !ao_blur_frag_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: AO shader functions not found");
            return false;
        }

        MTLRenderPipelineDescriptor* ao_desc = [[MTLRenderPipelineDescriptor alloc] init];
        ao_desc.vertexFunction = ao_vert_fn;
        ao_desc.fragmentFunction = ao_frag_fn;
        ao_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        id<MTLRenderPipelineState> ao_pso = [device newRenderPipelineStateWithDescriptor:ao_desc error:&error];
        if (!ao_pso)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create AO pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        ao_pipeline.reset(ao_pso);

        MTLRenderPipelineDescriptor* ao_blur_desc = [[MTLRenderPipelineDescriptor alloc] init];
        ao_blur_desc.vertexFunction = ao_vert_fn;
        ao_blur_desc.fragmentFunction = ao_blur_frag_fn;
        ao_blur_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        id<MTLRenderPipelineState> ao_blur_pso = [device newRenderPipelineStateWithDescriptor:ao_blur_desc error:&error];
        if (!ao_blur_pso)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create AO blur pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        ao_blur_pipeline.reset(ao_blur_pso);

        MTLSamplerDescriptor* gbuffer_sampler_desc = [[MTLSamplerDescriptor alloc] init];
        gbuffer_sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        gbuffer_sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        gbuffer_sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        gbuffer_sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_sampler_desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_sampler.reset([device newSamplerStateWithDescriptor:gbuffer_sampler_desc]);
        if (!gbuffer_sampler)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create GBuffer sampler");
            return false;
        }

        MTLSamplerDescriptor* gbuffer_point_sampler_desc = [[MTLSamplerDescriptor alloc] init];
        gbuffer_point_sampler_desc.minFilter = MTLSamplerMinMagFilterNearest;
        gbuffer_point_sampler_desc.magFilter = MTLSamplerMinMagFilterNearest;
        gbuffer_point_sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        gbuffer_point_sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_point_sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_point_sampler_desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_point_sampler.reset([device newSamplerStateWithDescriptor:gbuffer_point_sampler_desc]);
        if (!gbuffer_point_sampler)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create point-sampled GBuffer sampler");
            return false;
        }
        DRAXUL_LOG_INFO(LogCategory::App, "MegaCity: GBuffer pipeline initialized");
        return true;
    }

    bool ensure_gbuffer_targets(id<MTLDevice> device, uint32_t frame_count, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;

        frame_count = std::max(1u, frame_count);
        if (gbuffer_targets.size() == frame_count
            && !gbuffer_targets.empty()
            && gbuffer_targets[0].width == width
            && gbuffer_targets[0].height == height)
            return true;

        gbuffer_targets.clear();
        gbuffer_targets.resize(frame_count);

        for (auto& targets : gbuffer_targets)
        {
            MTLTextureDescriptor* shadow_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:static_cast<NSUInteger>(shadow_map_resolution)
                                            height:static_cast<NSUInteger>(shadow_map_resolution)
                                         mipmapped:NO];
            shadow_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            shadow_desc.storageMode = MTLStorageModePrivate;

            MTLTextureDescriptor* rgba8_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            rgba8_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            rgba8_desc.storageMode = MTLStorageModePrivate;

            MTLTextureDescriptor* depth_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            depth_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            depth_desc.storageMode = MTLStorageModePrivate;

            MTLTextureDescriptor* scene_msaa_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            scene_msaa_desc.textureType = MTLTextureType2DMultisample;
            scene_msaa_desc.sampleCount = scene_sample_count;
            scene_msaa_desc.usage = MTLTextureUsageRenderTarget;
            scene_msaa_desc.storageMode = MTLStorageModePrivate;

            MTLTextureDescriptor* scene_depth_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            scene_depth_desc.textureType = MTLTextureType2DMultisample;
            scene_depth_desc.sampleCount = scene_sample_count;
            scene_depth_desc.usage = MTLTextureUsageRenderTarget;
            scene_depth_desc.storageMode = MTLStorageModePrivate;

            MTLTextureDescriptor* scene_hdr_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            scene_hdr_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            scene_hdr_desc.storageMode = MTLStorageModePrivate;

            MTLTextureDescriptor* scene_final_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            scene_final_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            scene_final_desc.storageMode = MTLStorageModePrivate;

            targets.normal.reset([device newTextureWithDescriptor:rgba8_desc]);
            targets.ao_raw.reset([device newTextureWithDescriptor:rgba8_desc]);
            targets.ao.reset([device newTextureWithDescriptor:rgba8_desc]);
            targets.depth.reset([device newTextureWithDescriptor:depth_desc]);
            for (auto& shadow_map : targets.shadow_maps)
                shadow_map.reset([device newTextureWithDescriptor:shadow_desc]);
            targets.scene_color_msaa.reset([device newTextureWithDescriptor:scene_msaa_desc]);
            targets.scene_depth_msaa.reset([device newTextureWithDescriptor:scene_depth_desc]);
            targets.scene_hdr.reset([device newTextureWithDescriptor:scene_hdr_desc]);
            targets.scene_final_srgb.reset([device newTextureWithDescriptor:scene_final_desc]);
            if (targets.scene_final_srgb)
            {
                targets.scene_final_unorm.reset(
                    [targets.scene_final_srgb.get() newTextureViewWithPixelFormat:MTLPixelFormatBGRA8Unorm]);
            }
            targets.width = width;
            targets.height = height;

            const bool have_shadow_maps = std::all_of(
                targets.shadow_maps.begin(), targets.shadow_maps.end(),
                [](const ObjCRef<id<MTLTexture>>& texture) { return texture.get() != nil; });

            if (!targets.normal || !targets.ao_raw || !targets.ao || !targets.depth || !have_shadow_maps
                || !targets.scene_color_msaa || !targets.scene_depth_msaa
                || !targets.scene_hdr || !targets.scene_final_srgb || !targets.scene_final_unorm)
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create GBuffer/scene textures");
                gbuffer_targets.clear();
                return false;
            }
        }
        return true;
    }
};

IsometricScenePass::IsometricScenePass(int grid_width, int grid_height, float tile_size)
    : grid_width_(grid_width)
    , grid_height_(grid_height)
    , tile_size_(tile_size)
    , state_(std::make_unique<State>())
{
}

IsometricScenePass::~IsometricScenePass() = default;

void IsometricScenePass::record_prepass(IRenderContext& ctx)
{
    id<MTLCommandBuffer> cmd_buf = (__bridge id<MTLCommandBuffer>)ctx.native_command_buffer();
    if (!cmd_buf)
        return;

    const int vw = ctx.viewport_w();
    const int vh = ctx.viewport_h();
    if (vw <= 0 || vh <= 0)
        return;

    const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
    if (!state_->init(cmd_buf.device, grid_width_, grid_height_, tile_size_))
        return;
    if (!state_->init_gbuffer(cmd_buf.device))
        return;
    if (!state_->ensure_frame_resources(cmd_buf.device, frame_count))
        return;
    if (!state_->ensure_gbuffer_targets(cmd_buf.device, frame_count, vw, vh))
        return;
    if (!state_->ensure_road_materials(cmd_buf))
        return;
    if (!state_->ensure_label_atlas(cmd_buf.device, scene_.label_atlas))
        return;
    if (!state_->ensure_floor_grid(cmd_buf.device, scene_.floor_grid))
        return;

    const uint32_t frame_index = ctx.frame_index() % static_cast<uint32_t>(state_->frame_resources.size());
    state_->last_prepass_frame = frame_index;
    auto& frame_resources = state_->frame_resources[frame_index];
    frame_resources.geometry_arena.reset();

    if (!state_->ensure_tree_mesh(cmd_buf.device, scene_.tree_bark_mesh, scene_.tree_leaf_mesh)
        || !state_->ensure_custom_meshes(cmd_buf.device, scene_.custom_meshes))
        return;

    MeshSlice grid_slice;
    if (state_->has_cached_grid_mesh
        && !stream_transient_mesh(cmd_buf.device, state_->cached_grid_mesh, frame_resources.geometry_arena, grid_slice))
    {
        return;
    }

    auto& gbuffer = state_->gbuffer_targets[frame_index];

    MTLViewport viewport;
    viewport.originX = 0;
    viewport.originY = 0;
    viewport.width = vw;
    viewport.height = vh;
    viewport.znear = 0.0;
    viewport.zfar = 1.0;

    MTLScissorRect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = static_cast<NSUInteger>(vw);
    scissor.height = static_cast<NSUInteger>(vh);

    // Upload frame uniforms (same as main pass)
    FrameUniforms frame;
    frame.view = to_simd_matrix(scene_.camera.view);
    frame.proj = to_simd_matrix(scene_.camera.proj);
    frame.inv_view_proj = to_simd_matrix(scene_.camera.inv_view_proj);
    frame.camera_pos = simd_make_float4(
        scene_.camera.camera_pos.x, scene_.camera.camera_pos.y,
        scene_.camera.camera_pos.z, scene_.camera.camera_pos.w);
    frame.light_dir = simd_make_float4(
        scene_.camera.light_dir.x, scene_.camera.light_dir.y,
        scene_.camera.light_dir.z, scene_.camera.light_dir.w);
    frame.point_light_pos = simd_make_float4(
        scene_.camera.point_light_pos.x, scene_.camera.point_light_pos.y,
        scene_.camera.point_light_pos.z, scene_.camera.point_light_pos.w);
    frame.label_fade_px = simd_make_float4(
        scene_.camera.label_fade_px.x,
        scene_.camera.label_fade_px.y,
        scene_.camera.label_fade_px.z,
        scene_.camera.label_fade_px.w);
    frame.render_tuning = simd_make_float4(
        scene_.camera.render_tuning.x,
        scene_.camera.render_tuning.y,
        scene_.camera.render_tuning.z,
        scene_.camera.render_tuning.w);
    frame.screen_params = simd_make_float4(0.0f, 0.0f, 1.0f / std::max(vw, 1), 1.0f / std::max(vh, 1));
    frame.ao_params = simd_make_float4(
        scene_.camera.ao_settings.x,
        compute_ao_radius_pixels(scene_.camera.proj, scene_.camera.ao_settings.x, vh),
        scene_.camera.ao_settings.y,
        scene_.camera.ao_settings.z);
    frame.debug_view = simd_make_float4(
        scene_.camera.debug_view.x,
        scene_.camera.debug_view.y,
        scene_.camera.debug_view.z,
        scene_.camera.debug_view.w);
    frame.world_debug_bounds = simd_make_float4(
        scene_.camera.world_debug_bounds.x,
        scene_.camera.world_debug_bounds.y,
        scene_.camera.world_debug_bounds.z,
        scene_.camera.world_debug_bounds.w);
    const DirectionalShadowCascadeSet shadow_cascades = build_directional_shadow_cascades(scene_.camera, state_->shadow_map_resolution);
    for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
    {
        const glm::mat4 world_to_clip = shadow_cascades.cascades[cascade_index].proj * shadow_cascades.cascades[cascade_index].view;
        frame.shadow_view_proj[cascade_index] = to_simd_matrix(world_to_clip);
        frame.shadow_texture_matrix[cascade_index] = to_simd_matrix(shadow_texture_matrix(world_to_clip));
    }
    frame.shadow_split_depths = simd_make_float4(
        shadow_cascades.cascades[0].split_depth,
        shadow_cascades.cascades[1].split_depth,
        shadow_cascades.cascades[2].split_depth,
        1.0f);
    frame.shadow_params = simd_make_float4(
        static_cast<float>(shadow_cascades.cascade_count),
        shadow_cascades.sample_depth_bias,
        shadow_cascades.normal_bias,
        1.0f / static_cast<float>(std::max(shadow_cascades.resolution, 1)));
    std::memcpy([frame_resources.frame_uniforms.get() contents], &frame, sizeof(frame));

    const uint32_t gbuffer_opaque_count = std::min(scene_.opaque_count,
        static_cast<uint32_t>(scene_.objects.size()));
    auto object_casts_shadow = [&](const SceneObject& obj) {
        if (obj.mesh == MeshId::Grid || obj.color.a < 1.0f)
            return false;
        if (!obj.route_source.empty() || !obj.route_target.empty())
            return false;
        if (obj.material_index < scene_.materials.size()
            && scene_.materials[obj.material_index].shading_model == MaterialShadingModel::LeafCutoutPbr)
        {
            return false;
        }
        return true;
    };

    for (uint32_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
    {
        if (!gbuffer.shadow_maps[cascade_index])
            continue;

        MTLRenderPassDescriptor* shadowDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        shadowDesc.depthAttachment.texture = gbuffer.shadow_maps[cascade_index].get();
        shadowDesc.depthAttachment.loadAction = MTLLoadActionClear;
        shadowDesc.depthAttachment.storeAction = MTLStoreActionStore;
        shadowDesc.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> shadowEncoder = [cmd_buf renderCommandEncoderWithDescriptor:shadowDesc];
        if (!shadowEncoder)
            return;

        MTLViewport shadowViewport;
        shadowViewport.originX = 0.0;
        shadowViewport.originY = 0.0;
        shadowViewport.width = static_cast<double>(state_->shadow_map_resolution);
        shadowViewport.height = static_cast<double>(state_->shadow_map_resolution);
        shadowViewport.znear = 0.0;
        shadowViewport.zfar = 1.0;
        [shadowEncoder setViewport:shadowViewport];

        MTLScissorRect shadowScissor;
        shadowScissor.x = 0;
        shadowScissor.y = 0;
        shadowScissor.width = static_cast<NSUInteger>(state_->shadow_map_resolution);
        shadowScissor.height = static_cast<NSUInteger>(state_->shadow_map_resolution);
        [shadowEncoder setScissorRect:shadowScissor];
        [shadowEncoder setRenderPipelineState:state_->shadow_pipeline.get()];
        [shadowEncoder setDepthStencilState:state_->depth_state.get()];
        [shadowEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [shadowEncoder setDepthBias:2.0 slopeScale:2.0 clamp:0.02];
        [shadowEncoder setVertexBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];

        for (uint32_t gi = 0; gi < gbuffer_opaque_count; ++gi)
        {
            const SceneObject& obj = scene_.objects[gi];
            if (!object_casts_shadow(obj))
                continue;

            const MeshBuffers* mesh = nullptr;
            switch (obj.mesh)
            {
            case MeshId::Floor:
                mesh = &state_->floor_mesh;
                break;
            case MeshId::Cube:
                mesh = &state_->cube_mesh;
                break;
            case MeshId::TreeBark:
                mesh = &state_->tree_bark_mesh;
                break;
            case MeshId::TreeLeaves:
                mesh = &state_->tree_leaf_mesh;
                break;
            case MeshId::RoadSurface:
                mesh = &state_->road_surface_mesh;
                break;
            case MeshId::RoofSign:
                mesh = &state_->roof_sign_mesh;
                break;
            case MeshId::WallSign:
                mesh = &state_->wall_sign_mesh;
                break;
            case MeshId::Custom:
                if (obj.custom_mesh_index < state_->custom_meshes.size())
                    mesh = &state_->custom_meshes[obj.custom_mesh_index];
                break;
            case MeshId::Grid:
                break;
            }
            if (!mesh || mesh->index_count == 0)
                continue;

            ObjectUniforms object;
            object.world = to_simd_matrix(obj.world);
            object.color = simd_make_float4(obj.color.x, obj.color.y, obj.color.z, obj.color.w);
            object.material_data = simd_make_uint4(obj.material_index, cascade_index, 0u, 0u);
            object.uv_rect = simd_make_float4(obj.uv_rect.x, obj.uv_rect.y, obj.uv_rect.z, obj.uv_rect.w);
            object.label_metrics = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

            [shadowEncoder setCullMode:obj.double_sided ? MTLCullModeNone : MTLCullModeBack];
            [shadowEncoder setVertexBuffer:mesh->vertex_buffer.get() offset:0 atIndex:0];
            [shadowEncoder setVertexBytes:&object length:sizeof(object) atIndex:2];
            [shadowEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                      indexCount:mesh->index_count
                                       indexType:MTLIndexTypeUInt16
                                     indexBuffer:mesh->index_buffer.get()
                               indexBufferOffset:0];
        }

        [shadowEncoder endEncoding];
    }

    // Build GBuffer render pass descriptor
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = gbuffer.normal.get();
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.5, 0.5, 0.0, 0.0);
    rpDesc.depthAttachment.texture = gbuffer.depth.get();
    rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
    rpDesc.depthAttachment.storeAction = MTLStoreActionStore;
    rpDesc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder = [cmd_buf renderCommandEncoderWithDescriptor:rpDesc];
    if (!encoder)
        return;

    [encoder setViewport:viewport];
    [encoder setScissorRect:scissor];

    [encoder setRenderPipelineState:state_->gbuffer_pipeline.get()];
    [encoder setDepthStencilState:state_->depth_state.get()];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setVertexBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];
    [encoder setFragmentBuffer:frame_resources.material_uniforms.get() offset:0 atIndex:3];
    for (NSUInteger texture_index = 0; texture_index < state_->material_textures.size(); ++texture_index)
        [encoder setFragmentTexture:state_->material_textures[texture_index].get() atIndex:2 + texture_index];
    [encoder setFragmentSamplerState:state_->material_sampler.get() atIndex:2];

    // Draw scene objects (GBuffer: opaque only)
    for (uint32_t gi = 0; gi < gbuffer_opaque_count; ++gi)
    {
        const SceneObject& obj = scene_.objects[gi];
        const MeshBuffers* mesh = nullptr;
        switch (obj.mesh)
        {
        case MeshId::Floor:
            mesh = &state_->floor_mesh;
            break;
        case MeshId::Cube:
            mesh = &state_->cube_mesh;
            break;
        case MeshId::TreeBark:
            mesh = &state_->tree_bark_mesh;
            break;
        case MeshId::TreeLeaves:
            mesh = &state_->tree_leaf_mesh;
            break;
        case MeshId::RoadSurface:
            mesh = &state_->road_surface_mesh;
            break;
        case MeshId::RoofSign:
            mesh = &state_->roof_sign_mesh;
            break;
        case MeshId::WallSign:
            mesh = &state_->wall_sign_mesh;
            break;
        case MeshId::Custom:
            if (obj.custom_mesh_index < state_->custom_meshes.size())
                mesh = &state_->custom_meshes[obj.custom_mesh_index];
            break;
        case MeshId::Grid:
            continue;
        }
        if (!mesh || mesh->index_count == 0)
            continue;

        ObjectUniforms object;
        object.world = to_simd_matrix(obj.world);
        object.color = simd_make_float4(obj.color.x, obj.color.y, obj.color.z, obj.color.w);
        object.material_data = simd_make_uint4(obj.material_index, 0u, 0u, 0u);
        object.uv_rect = simd_make_float4(obj.uv_rect.x, obj.uv_rect.y, obj.uv_rect.z, obj.uv_rect.w);
        object.label_metrics = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

        [encoder setCullMode:obj.double_sided ? MTLCullModeNone : MTLCullModeBack];
        [encoder setVertexBuffer:mesh->vertex_buffer.get() offset:0 atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:mesh->index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:mesh->index_buffer.get()
                     indexBufferOffset:0];
    }

    // Draw floor grid
    if (grid_slice.index_count > 0)
    {
        ObjectUniforms object;
        object.world = matrix_identity_float4x4;
        object.color = simd_make_float4(
            scene_.floor_grid.color.x, scene_.floor_grid.color.y,
            scene_.floor_grid.color.z, scene_.floor_grid.color.w);
        object.material_data = simd_make_uint4(0u, 0u, 0u, 0u);
        object.uv_rect = simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);
        object.label_metrics = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

        [encoder setVertexBuffer:grid_slice.vertex_buffer offset:grid_slice.vertex_offset atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:grid_slice.index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:grid_slice.index_buffer
                     indexBufferOffset:grid_slice.index_offset];
    }

    [encoder endEncoding];

    MTLRenderPassDescriptor* aoDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    aoDesc.colorAttachments[0].texture = gbuffer.ao_raw.get();
    aoDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    aoDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> aoEncoder = [cmd_buf renderCommandEncoderWithDescriptor:aoDesc];
    if (!aoEncoder)
        return;

    [aoEncoder setViewport:viewport];
    [aoEncoder setScissorRect:scissor];
    [aoEncoder setRenderPipelineState:state_->ao_pipeline.get()];
    [aoEncoder setCullMode:MTLCullModeNone];
    [aoEncoder setFragmentBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:0];
    [aoEncoder setFragmentTexture:gbuffer.normal.get() atIndex:0];
    [aoEncoder setFragmentTexture:gbuffer.depth.get() atIndex:1];
    [aoEncoder setFragmentSamplerState:state_->gbuffer_point_sampler.get() atIndex:0];
    [aoEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [aoEncoder endEncoding];

    MTLRenderPassDescriptor* blurDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    blurDesc.colorAttachments[0].texture = gbuffer.ao.get();
    blurDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    blurDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> blurEncoder = [cmd_buf renderCommandEncoderWithDescriptor:blurDesc];
    if (!blurEncoder)
        return;

    [blurEncoder setViewport:viewport];
    [blurEncoder setScissorRect:scissor];
    [blurEncoder setRenderPipelineState:state_->ao_blur_pipeline.get()];
    [blurEncoder setCullMode:MTLCullModeNone];
    [blurEncoder setFragmentBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:0];
    [blurEncoder setFragmentTexture:gbuffer.ao_raw.get() atIndex:0];
    [blurEncoder setFragmentTexture:gbuffer.normal.get() atIndex:1];
    [blurEncoder setFragmentTexture:gbuffer.depth.get() atIndex:2];
    [blurEncoder setFragmentSamplerState:state_->gbuffer_point_sampler.get() atIndex:0];
    [blurEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [blurEncoder endEncoding];

    MTLRenderPassDescriptor* sceneDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    sceneDesc.colorAttachments[0].texture = gbuffer.scene_color_msaa.get();
    sceneDesc.colorAttachments[0].resolveTexture = gbuffer.scene_hdr.get();
    sceneDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    sceneDesc.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    sceneDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    sceneDesc.depthAttachment.texture = gbuffer.scene_depth_msaa.get();
    sceneDesc.depthAttachment.loadAction = MTLLoadActionClear;
    sceneDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
    sceneDesc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> sceneEncoder = [cmd_buf renderCommandEncoderWithDescriptor:sceneDesc];
    if (!sceneEncoder)
        return;

    [sceneEncoder setViewport:viewport];
    [sceneEncoder setScissorRect:scissor];

    const MaterialUniforms material_uniforms = build_material_uniforms(scene_);
    std::memcpy([frame_resources.material_uniforms.get() contents], &material_uniforms, sizeof(material_uniforms));

    const int debug_mode = static_cast<int>(scene_.camera.debug_view.x + 0.5f);
    const bool use_debug = debug_mode > 0 && state_->debug_pipeline.get() != nil;
    [sceneEncoder setRenderPipelineState:use_debug ? state_->debug_pipeline.get() : state_->pipeline.get()];
    [sceneEncoder setDepthStencilState:state_->depth_state.get()];
    [sceneEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
    const bool wireframe = scene_.camera.debug_view.w > 0.5f;
    [sceneEncoder setTriangleFillMode:wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
    [sceneEncoder setVertexBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];
    [sceneEncoder setFragmentBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];
    [sceneEncoder setFragmentBuffer:frame_resources.material_uniforms.get() offset:0 atIndex:3];
    [sceneEncoder setFragmentTexture:state_->label_atlas_texture.get() atIndex:0];
    id<MTLTexture> ao_texture = (debug_mode == 1)
        ? gbuffer.ao_raw.get()
        : gbuffer.ao.get();
    [sceneEncoder setFragmentTexture:ao_texture atIndex:1];
    for (NSUInteger texture_index = 0; texture_index < state_->material_textures.size(); ++texture_index)
        [sceneEncoder setFragmentTexture:state_->material_textures[texture_index].get() atIndex:2 + texture_index];
    for (NSUInteger cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
        [sceneEncoder setFragmentTexture:gbuffer.shadow_maps[cascade_index].get() atIndex:27 + cascade_index];
    [sceneEncoder setFragmentSamplerState:state_->label_sampler.get() atIndex:0];
    [sceneEncoder setFragmentSamplerState:state_->gbuffer_sampler.get() atIndex:1];
    [sceneEncoder setFragmentSamplerState:state_->material_sampler.get() atIndex:2];
    [sceneEncoder setFragmentSamplerState:state_->gbuffer_point_sampler.get() atIndex:3];

    auto draw_scene_object = [&](const SceneObject& obj) {
        const MeshBuffers* mesh = nullptr;
        switch (obj.mesh)
        {
        case MeshId::Floor:
            mesh = &state_->floor_mesh;
            break;
        case MeshId::Cube:
            mesh = &state_->cube_mesh;
            break;
        case MeshId::TreeBark:
            mesh = &state_->tree_bark_mesh;
            break;
        case MeshId::TreeLeaves:
            mesh = &state_->tree_leaf_mesh;
            break;
        case MeshId::RoadSurface:
            mesh = &state_->road_surface_mesh;
            break;
        case MeshId::RoofSign:
            mesh = &state_->roof_sign_mesh;
            break;
        case MeshId::WallSign:
            mesh = &state_->wall_sign_mesh;
            break;
        case MeshId::Custom:
            if (obj.custom_mesh_index < state_->custom_meshes.size())
                mesh = &state_->custom_meshes[obj.custom_mesh_index];
            break;
        case MeshId::Grid:
            return;
        }
        if (!mesh || mesh->index_count == 0)
            return;

        ObjectUniforms object;
        object.world = to_simd_matrix(obj.world);
        object.color = simd_make_float4(obj.color.x, obj.color.y, obj.color.z, obj.color.w);
        object.material_data = simd_make_uint4(obj.material_index, 0u, 0u, 0u);
        object.uv_rect = simd_make_float4(obj.uv_rect.x, obj.uv_rect.y, obj.uv_rect.z, obj.uv_rect.w);
        object.label_metrics = simd_make_float4(
            obj.label_ink_pixel_size.x,
            obj.label_ink_pixel_size.y,
            0.0f,
            0.0f);

        [sceneEncoder setCullMode:obj.double_sided ? MTLCullModeNone : MTLCullModeBack];
        [sceneEncoder setVertexBuffer:mesh->vertex_buffer.get() offset:0 atIndex:0];
        [sceneEncoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [sceneEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                 indexCount:mesh->index_count
                                  indexType:MTLIndexTypeUInt16
                                indexBuffer:mesh->index_buffer.get()
                          indexBufferOffset:0];
    };

    const uint32_t opaque_count = std::min(scene_.opaque_count,
        static_cast<uint32_t>(scene_.objects.size()));
    for (uint32_t i = 0; i < opaque_count; ++i)
        draw_scene_object(scene_.objects[i]);

    if (opaque_count < scene_.objects.size())
    {
        [sceneEncoder setDepthStencilState:state_->depth_state_no_write.get()];
        for (uint32_t i = opaque_count; i < scene_.objects.size(); ++i)
            draw_scene_object(scene_.objects[i]);
        [sceneEncoder setDepthStencilState:state_->depth_state.get()];
    }

    if (grid_slice.index_count > 0)
    {
        ObjectUniforms object;
        object.world = matrix_identity_float4x4;
        object.color = simd_make_float4(
            scene_.floor_grid.color.x,
            scene_.floor_grid.color.y,
            scene_.floor_grid.color.z,
            scene_.floor_grid.color.w);
        object.material_data = simd_make_uint4(0u, 0u, 0u, 0u);
        object.uv_rect = simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);
        object.label_metrics = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

        [sceneEncoder setVertexBuffer:grid_slice.vertex_buffer offset:grid_slice.vertex_offset atIndex:0];
        [sceneEncoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [sceneEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                 indexCount:grid_slice.index_count
                                  indexType:MTLIndexTypeUInt16
                                indexBuffer:grid_slice.index_buffer
                          indexBufferOffset:grid_slice.index_offset];
    }

    if (wireframe)
        [sceneEncoder setTriangleFillMode:MTLTriangleFillModeFill];
    [sceneEncoder endEncoding];

    MTLRenderPassDescriptor* postDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    postDesc.colorAttachments[0].texture = gbuffer.scene_final_srgb.get();
    postDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    postDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> postEncoder = [cmd_buf renderCommandEncoderWithDescriptor:postDesc];
    if (!postEncoder)
        return;

    [postEncoder setViewport:viewport];
    [postEncoder setScissorRect:scissor];
    [postEncoder setRenderPipelineState:state_->post_pipeline.get()];
    [postEncoder setCullMode:MTLCullModeNone];
    [postEncoder setFragmentBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:0];
    [postEncoder setFragmentTexture:gbuffer.scene_hdr.get() atIndex:0];
    [postEncoder setFragmentSamplerState:state_->gbuffer_sampler.get() atIndex:0];
    [postEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [postEncoder endEncoding];
}

void IsometricScenePass::record(IRenderContext& ctx)
{
    id<MTLCommandBuffer> cmd_buf = (__bridge id<MTLCommandBuffer>)ctx.native_command_buffer();
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)ctx.native_render_encoder();
    if (!cmd_buf || !encoder)
        return;

    const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
    if (!state_->init(cmd_buf.device, grid_width_, grid_height_, tile_size_))
        return;
    if (!state_->ensure_gbuffer_targets(cmd_buf.device, frame_count, ctx.viewport_w(), ctx.viewport_h()))
        return;
    const uint32_t frame_index = ctx.frame_index() % static_cast<uint32_t>(state_->gbuffer_targets.size());
    auto& gbuffer = state_->gbuffer_targets[frame_index];
    if (!gbuffer.scene_final_unorm)
        return;

    MTLViewport viewport;
    viewport.originX = ctx.viewport_x();
    viewport.originY = ctx.viewport_y();
    viewport.width = ctx.viewport_w();
    viewport.height = ctx.viewport_h();
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    [encoder setViewport:viewport];

    MTLScissorRect scissor;
    scissor.x = static_cast<NSUInteger>(std::max(0, ctx.viewport_x()));
    scissor.y = static_cast<NSUInteger>(std::max(0, ctx.viewport_y()));
    scissor.width = static_cast<NSUInteger>(std::max(0, ctx.viewport_w()));
    scissor.height = static_cast<NSUInteger>(std::max(0, ctx.viewport_h()));
    [encoder setScissorRect:scissor];
    [encoder setRenderPipelineState:state_->present_pipeline.get()];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setFragmentTexture:gbuffer.scene_final_unorm.get() atIndex:0];
    [encoder setFragmentSamplerState:state_->gbuffer_sampler.get() atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

    // Draw tooltip overlay if visible.
    if (scene_.tooltip.valid() && state_->tooltip_pipeline.get() != nil && state_->tooltip_sampler.get() != nil)
    {
        // Upload / re-upload tooltip texture if revision changed.
        if (state_->tooltip_texture_revision != scene_.tooltip.revision || !state_->tooltip_texture.get())
        {
            MTLTextureDescriptor* tex_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:static_cast<NSUInteger>(scene_.tooltip.width)
                                            height:static_cast<NSUInteger>(scene_.tooltip.height)
                                         mipmapped:NO];
            tex_desc.usage = MTLTextureUsageShaderRead;
            id<MTLTexture> tex = [cmd_buf.device newTextureWithDescriptor:tex_desc];
            if (tex)
            {
                [tex replaceRegion:MTLRegionMake2D(0, 0,
                                       static_cast<NSUInteger>(scene_.tooltip.width),
                                       static_cast<NSUInteger>(scene_.tooltip.height))
                       mipmapLevel:0
                         withBytes:scene_.tooltip.rgba.data()
                       bytesPerRow:static_cast<NSUInteger>(scene_.tooltip.width * 4)];
                state_->tooltip_texture.reset(tex);
                state_->tooltip_texture_revision = scene_.tooltip.revision;
            }
        }

        if (state_->tooltip_texture.get())
        {
            struct TooltipUniforms
            {
                simd_float4 rect;
                simd_float4 viewport;
            };

            const float full_w = static_cast<float>(ctx.width());
            const float full_h = static_cast<float>(ctx.height());

            TooltipUniforms uniforms;
            uniforms.rect = simd_make_float4(
                scene_.tooltip.screen_pos.x,
                scene_.tooltip.screen_pos.y,
                static_cast<float>(scene_.tooltip.width),
                static_cast<float>(scene_.tooltip.height));
            uniforms.viewport = simd_make_float4(full_w, full_h, 0.0f, 0.0f);

            // Use full-window viewport for screen-space positioning.
            MTLViewport full_viewport;
            full_viewport.originX = 0;
            full_viewport.originY = 0;
            full_viewport.width = full_w;
            full_viewport.height = full_h;
            full_viewport.znear = 0.0;
            full_viewport.zfar = 1.0;
            [encoder setViewport:full_viewport];

            MTLScissorRect full_scissor;
            full_scissor.x = 0;
            full_scissor.y = 0;
            full_scissor.width = static_cast<NSUInteger>(ctx.width());
            full_scissor.height = static_cast<NSUInteger>(ctx.height());
            [encoder setScissorRect:full_scissor];

            [encoder setRenderPipelineState:state_->tooltip_pipeline.get()];
            [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
            [encoder setFragmentTexture:state_->tooltip_texture.get() atIndex:0];
            [encoder setFragmentSamplerState:state_->tooltip_sampler.get() atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        }
    }
}

void IsometricScenePass::render_gbuffer_debug_ui()
{
    if (state_->gbuffer_targets.empty())
        return;

    const uint32_t fi = state_->last_prepass_frame % static_cast<uint32_t>(state_->gbuffer_targets.size());
    auto& t = state_->gbuffer_targets[fi];
    if (!t.normal || !t.ao_raw || !t.ao || !t.depth || !t.scene_hdr || !t.scene_final_unorm)
        return;

    if (!ImGui::Begin("GBuffer Debug"))
    {
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float aspect = t.height > 0 ? static_cast<float>(t.width) / static_cast<float>(t.height) : 1.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float text_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float cell_w = std::max(64.0f, (avail.x - spacing) * 0.5f);
    const float cell_h = std::max(32.0f, (avail.y - text_h * 5.0f) * 0.5f);
    const float img_w = std::min(cell_w, cell_h * aspect);
    const float img_h = img_w / aspect;
    const ImVec2 size(img_w, img_h);

    if (ImGui::BeginTable("##gbuffer_grid", 2))
    {
        ImGui::TableNextColumn();
        ImGui::Text("Normals");
        ImGui::Image((__bridge void*)t.normal.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("Raw AO");
        ImGui::Image((__bridge void*)t.ao_raw.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("Depth");
        ImGui::Image((__bridge void*)t.depth.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("Ambient Occlusion");
        ImGui::Image((__bridge void*)t.ao.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("Scene HDR");
        ImGui::Image((__bridge void*)t.scene_hdr.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("Scene Final");
        ImGui::Image((__bridge void*)t.scene_final_unorm.get(), size, ImVec2(0, 1), ImVec2(1, 0));

        for (NSUInteger cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
        {
            ImGui::TableNextColumn();
            ImGui::Text("Shadow %u", static_cast<unsigned>(cascade_index));
            ImGui::Image((__bridge void*)t.shadow_maps[cascade_index].get(), size);
        }

        ImGui::EndTable();
    }

    ImGui::Text("Size: %dx%d", t.width, t.height);
    ImGui::End();
}

} // namespace draxul
