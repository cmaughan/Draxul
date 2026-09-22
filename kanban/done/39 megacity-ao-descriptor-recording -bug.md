# Select AO descriptors before recording their use

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:3799` and `:3953` update a descriptor set already bound at `:3667`. Raw-AO debug mode therefore invalidates recorded commands under the [Vulkan descriptor-update rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html).

**Investigation**

- [x] Trace binding 3 through GBuffer, scene, and debug passes.

**Fix strategy**

- [x] Select and write descriptors before their first recorded binding.
- [x] Use separate descriptor sets where passes require different images. (Not required: binding 3 has one frame-wide raw/denoised selection.)

**Acceptance criteria**

- [x] Raw and denoised AO views use their intended images.
- [x] Toggling debug modes produces no descriptor-update validation errors.
- [x] Preserve Metal’s corresponding visual behavior.

**Validation evidence (2026-09-22)**

- `py do.py test debug --megacity` passed all 27 selected core and MegaCity CTest entries.
- Debug Vulkan validation-layer City and Biology runs exercised the per-frame AO descriptor selection before its first bind across both buffered frame slots. The live GBuffer debug view displayed distinct raw and denoised AO images, and the logs contained no descriptor-update errors or `vkUpdateDescriptorSets` VUIDs. The only validation output was the pre-existing unused-vertex-input performance warning.
