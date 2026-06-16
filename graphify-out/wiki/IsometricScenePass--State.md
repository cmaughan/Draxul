# IsometricScenePass::State

> God node · 129 connections · `megacity/draxul-megacity/src/megacity_render_vk.cpp`

**Community:** [[App Config IO]]

## Connections by Relation

### contains
- [[megacity_render_vk.cpp]] `EXTRACTED`

### defines
- [[buffered_frame_count]] `EXTRACTED`
- [[allocator]] `EXTRACTED`
- [[device]] `EXTRACTED`
- [[physical_device]] `EXTRACTED`
- [[render_pass]] `EXTRACTED`
- [[ao_blur_pipeline]] `EXTRACTED`
- [[ao_pipeline]] `EXTRACTED`
- [[ao_render_pass]] `EXTRACTED`
- [[cached_grid_mesh]] `EXTRACTED`
- [[cached_grid_spec]] `EXTRACTED`
- [[cube_mesh]] `EXTRACTED`
- [[custom_index_pool]] `EXTRACTED`
- [[custom_meshes]] `EXTRACTED`
- [[custom_vertex_pool]] `EXTRACTED`
- [[debug_pipeline]] `EXTRACTED`
- [[debug_wireframe_pipeline]] `EXTRACTED`
- [[descriptor_pool]] `EXTRACTED`
- [[descriptor_set_layout]] `EXTRACTED`
- [[floor_mesh]] `EXTRACTED`
- [[frame_resources]] `EXTRACTED`

### method
- [[.ensure()]] `EXTRACTED`
- [[.ensure_road_materials()]] `EXTRACTED`
- [[.create_device_resources()]] `EXTRACTED`
- [[.ensure_gbuffer_targets()]] `EXTRACTED`
- [[.ensure_label_atlas()]] `EXTRACTED`
- [[.ensure_custom_meshes()]] `EXTRACTED`
- [[.ensure_tooltip_texture()]] `EXTRACTED`
- [[.ensure_retired_mapped_buffer_capacity()]] `EXTRACTED`
- [[.destroy()]] `EXTRACTED`
- [[.ensure_tree_mesh()]] `EXTRACTED`
- [[.retire_buffer()]] `EXTRACTED`
- [[.create_present_resources()]] `EXTRACTED`
- [[.destroy_present_resources()]] `EXTRACTED`
- [[.ensure_floor_grid()]] `EXTRACTED`
- [[.init_gbuffer()]] `EXTRACTED`
- [[.mark_all_label_descriptors_dirty()]] `EXTRACTED`
- [[.reclaim_retired_resources()]] `EXTRACTED`
- [[.refresh_gbuffer_descriptors()]] `EXTRACTED`
- [[.refresh_prepass_descriptors()]] `EXTRACTED`
- [[.retire_image()]] `EXTRACTED`

### references
- [[GBufferTargets]] `EXTRACTED`
- [[ImageResource]] `EXTRACTED`
- [[Buffer]] `EXTRACTED`
- [[FrameResources]] `EXTRACTED`
- [[VmaAllocator]] `EXTRACTED`
- [[MeshBuffers]] `EXTRACTED`
- [[VkDevice]] `EXTRACTED`
- [[array]] `EXTRACTED`
- [[RetiredBufferResource]] `EXTRACTED`
- [[RetiredMeshResource]] `EXTRACTED`
- [[RetiredImageResource]] `EXTRACTED`
- [[MeshData]] `EXTRACTED`
- [[VkPhysicalDevice]] `EXTRACTED`
- [[VkImageLayout]] `EXTRACTED`
- [[FloorGridSpec]] `EXTRACTED`
- [[VkSampleCountFlagBits]] `EXTRACTED`
- [[vector]] `EXTRACTED`
- [[VkSampler]] `EXTRACTED`
- [[kSceneMaterialTextureCount]] `EXTRACTED`
- [[VkDescriptorPool]] `EXTRACTED`

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*