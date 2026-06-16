# MarkdownRenderPass::State

> God node · 44 connections · `libs/draxul-markdown/src/markdown_render_pass_vk.cpp`

**Community:** [[Markdown Render Pass Vulkan]]

## Connections by Relation

### contains
- [[markdown_render_pass_vk.cpp]] `EXTRACTED`

### defines
- [[buffered_frame_count]] `EXTRACTED`
- [[allocator]] `EXTRACTED`
- [[device]] `EXTRACTED`
- [[render_pass]] `EXTRACTED`
- [[atlases]] `EXTRACTED`
- [[descriptor_pool]] `EXTRACTED`
- [[frames]] `EXTRACTED`
- [[glyph_descriptor_layout]] `EXTRACTED`
- [[glyph_pipeline]] `EXTRACTED`
- [[glyph_pipeline_layout]] `EXTRACTED`
- [[rect_descriptor_layout]] `EXTRACTED`
- [[rect_pipeline]] `EXTRACTED`
- [[rect_pipeline_layout]] `EXTRACTED`
- [[sampler]] `EXTRACTED`

### method
- [[.upload_atlases()]] `EXTRACTED`
- [[.ensure_device()]] `EXTRACTED`
- [[.ensure_pipelines()]] `EXTRACTED`
- [[.prepare_upload_copies()]] `EXTRACTED`
- [[.upload_instances()]] `EXTRACTED`
- [[.allocate_descriptor_set()]] `EXTRACTED`
- [[.create_pipeline()]] `EXTRACTED`
- [[.ensure_frame_resources()]] `EXTRACTED`
- [[.shutdown()]] `EXTRACTED`
- [[.update_glyph_descriptor()]] `EXTRACTED`
- [[.ensure_atlas_texture()]] `EXTRACTED`
- [[.update_rect_descriptor()]] `EXTRACTED`
- [[.destroy_pipelines()]] `EXTRACTED`
- [[.ensure_descriptor_layouts()]] `EXTRACTED`
- [[.ensure_pipeline_layouts()]] `EXTRACTED`
- [[.ensure_sampler()]] `EXTRACTED`
- [[.ensure_descriptor_pool()]] `EXTRACTED`

### references
- [[vector]] `EXTRACTED`
- [[VmaAllocator]] `EXTRACTED`
- [[FrameResources]] `EXTRACTED`
- [[VkDevice]] `EXTRACTED`
- [[AtlasTexture]] `EXTRACTED`
- [[map]] `EXTRACTED`
- [[VkDescriptorSetLayout]] `EXTRACTED`
- [[VkPipeline]] `EXTRACTED`
- [[VkPipelineLayout]] `EXTRACTED`
- [[VkDescriptorPool]] `EXTRACTED`
- [[VkRenderPass]] `EXTRACTED`
- [[VkSampler]] `EXTRACTED`

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*