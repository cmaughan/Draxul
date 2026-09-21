# Protect MegaCity mesh pools from in-flight rewrites

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:1006` rewrites persistent mesh pools on each preparation. A rebuild fitting existing capacity overwrites geometry still referenced by an earlier frame; retirement currently handles growth only.

**Investigation**

- [ ] Trace same-capacity scene updates through all buffered frame slots.

**Fix strategy**

- [ ] Use frame-owned pools or immutable geometry generations with completion-based retirement.
- [ ] Avoid rewriting unchanged geometry.

**Acceptance criteria**

- [ ] Same-size, shrinking, and growing rebuilds preserve in-flight data.
- [ ] Vulkan synchronization validation remains clean.
- [ ] Review Metal’s corresponding mesh lifetime.
