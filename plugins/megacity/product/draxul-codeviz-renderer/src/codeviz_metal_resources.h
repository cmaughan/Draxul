#pragma once

#import <Metal/Metal.h>

#include <draxul/codeviz_scene_types.h>
#include <draxul/metal/objc_ref.h>
#include <vector>

namespace draxul::codeviz_metal
{

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

    void reset() { head = 0; }
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

bool upload_mesh(id<MTLDevice> device, const MeshData& mesh, MeshBuffers& buffers);
bool ensure_buffer_capacity(id<MTLDevice> device, NSUInteger required_size,
    ObjCRef<id<MTLBuffer>>& buffer);
bool upload_performance_heat_values(id<MTLDevice> device,
    const std::vector<float>& values, ObjCRef<id<MTLBuffer>>& buffer);
bool stream_transient_mesh(id<MTLDevice> device, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice);

} // namespace draxul::codeviz_metal
