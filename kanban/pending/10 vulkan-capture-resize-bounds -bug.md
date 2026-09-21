# Keep capture dimensions tied to the recorded image

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1; GPT-6 Astra

`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1363` recreates the swapchain before readback. `:409` then uses its new extent to read the old capture allocation. Growing the surface can read beyond mapped storage; shrinking misinterprets the image.

**Investigation**

- [ ] Exercise capture with presentation-triggered swapchain recreation, growing and shrinking.

**Fix strategy**

- [ ] Retain recorded capture dimensions and allocation bounds through completion.
- [ ] Complete readback before recreation where appropriate, or explicitly cancel invalid captures.

**Acceptance criteria**

- [ ] Captures never read beyond their allocation.
- [ ] Returned dimensions describe the captured image.
- [ ] Inspect Metal dimension handling without assuming the same resize mechanism.
