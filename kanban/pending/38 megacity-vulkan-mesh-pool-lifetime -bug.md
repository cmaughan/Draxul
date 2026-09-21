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
- [ ] Vulkan synchronization validation remains clean.
- [x] Review Metal’s corresponding mesh lifetime.
