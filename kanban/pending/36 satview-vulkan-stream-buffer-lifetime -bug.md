# Keep SatView vertex streams valid across in-flight frames

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/render/satview_render_vk.cpp:833` rewrites shared mapped stream buffers while previous frames may read them. Growth destroys those buffers at `:801` before their GPU users finish.

**Investigation**

- [ ] Trace every stream buffer’s use across buffered frames and revision changes.

**Fix strategy**

- [ ] Use frame-owned buffers or immutable upload generations.
- [ ] Retire replaced allocations only after all referencing frame slots complete.

**Acceptance criteria**

- [ ] Same-capacity updates and growth produce no GPU lifetime or synchronization errors.
- [ ] Rapid marker/track updates render correctly with multiple frames in flight.
- [ ] Review equivalent Metal ownership.
