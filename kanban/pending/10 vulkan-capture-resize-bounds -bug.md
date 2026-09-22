# Keep capture dimensions tied to the recorded image

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1; GPT-6 Astra

`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1363` recreates the swapchain before readback. `:409` then uses its new extent to read the old capture allocation. Growing the surface can read beyond mapped storage; shrinking misinterprets the image.

**Investigation**

- [ ] Exercise capture with presentation-triggered swapchain recreation, growing and shrinking.

**Fix strategy**

- [x] Retain recorded capture dimensions and allocation bounds through completion.
- [x] Complete readback before recreation where appropriate, or explicitly cancel invalid captures.

**Acceptance criteria**

- [x] Captures never read beyond their allocation.
- [x] Returned dimensions describe the captured image.
- [x] Inspect Metal dimension handling without assuming the same resize mechanism.

**Implementation note (2026-09-21):** Vulkan records the copy extent and byte
count before submitting, validates it against the mapped allocation, and reads it
back before present can rebuild the swapchain. Metal now reports the drawable
texture extent used for its blit rather than a potentially newer window extent.
Focused renderer/grid tests passed on macOS (75 cases, 635 assertions). The
broader aggregate is currently blocked linking unrelated PCBView symbols, and
the same-cache smoke could not connect to the existing Draxul server.
Vulkan/Windows runtime validation is still required.

**Coverage note (2026-09-22):** Windows white-box coverage now supplies a
mapped allocation through the renderer's Vulkan operation seam. It verifies
that readback uses the recorded extent and byte count, invalidates only that
bounded range, and rejects a byte count larger than the allocation. The
resize/render runtime gate remains pending.

**Windows validation note (2026-09-22):** `py do.py test debug --products`
passed with the bounded-readback coverage in the normal MSVC/Vulkan build.
That aggregate does not force presentation-triggered swapchain growth or
shrink during capture, so the investigation box remains open.
