# Keep SatView vertex streams valid across in-flight frames

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/render/satview_render_vk.cpp:833` rewrites shared mapped stream buffers while previous frames may read them. Growth destroys those buffers at `:801` before their GPU users finish.

**Investigation**

- [x] Trace every stream buffer’s use across buffered frames and revision changes.

**Fix strategy**

- [x] Use frame-owned buffers or immutable upload generations.
- [x] Retire replaced allocations only after all referencing frame slots complete.

**Acceptance criteria**

- [x] Same-capacity updates and growth produce no GPU lifetime or synchronization errors.
- [x] Rapid marker/track updates render correctly with multiple frames in flight.
- [x] Review equivalent Metal ownership.

**Completion note (2026-09-22):** A source-contract regression verifies that
every Vulkan stream buffer and uploaded revision is owned by a buffered-frame
slot, that upload and draw use the same slot, and that both same-capacity writes
and the growth/replacement branch operate on that slot only. The focused test
passed with 22 assertions. A live Debug SatView pane then processed 120 rapid
time/selection actions followed by 30 alternating window resizes with a stream
invalidation on every resize. The UI remained responsive, rebuilt its Vulkan
swapchain for every size transition, and logged no Vulkan validation, SatView,
or renderer warning/error messages.
