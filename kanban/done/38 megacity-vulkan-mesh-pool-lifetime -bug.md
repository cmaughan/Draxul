# Protect MegaCity mesh pools from in-flight rewrites

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:1006` rewrites persistent mesh pools on each preparation. A rebuild fitting existing capacity overwrites geometry still referenced by an earlier frame; retirement currently handles growth only.

**Investigation**

- [x] Trace same-capacity scene updates through all buffered frame slots.

**Fix strategy**

- [x] Use frame-owned pools or immutable geometry generations with completion-based retirement.
- [x] Avoid rewriting unchanged geometry.

**Acceptance criteria**

- [x] Same-size, shrinking, and growing rebuilds preserve in-flight data.
- [x] Vulkan synchronization validation remains clean.
- [x] Review Metal’s corresponding mesh lifetime.

**Validation evidence (2026-09-22)**

- `py do.py test debug --megacity` passed all 27 selected core and MegaCity CTest entries.
- Debug Vulkan validation-layer runs loaded a deterministic one-file city, published its asynchronous custom-mesh generation, and rendered both buffered frame slots in City and Biology modes. Both runs completed without synchronization, lifetime, destroyed-resource, or in-use validation errors. The only validation output was the pre-existing unused-vertex-input performance warning.

