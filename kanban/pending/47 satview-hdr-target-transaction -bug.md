# Publish HDR targets only after complete initialization

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/render/satview_render_vk.cpp:1048` considers matching vector size and first-target dimensions sufficient. A later failure at `:1076`, `:1094`, or `:1107` leaves that condition true, allowing subsequent frames to use incomplete targets.

**Investigation**

- [ ] Inject failures after the first target succeeds and within later attachment/descriptor creation.

**Fix strategy**

- [ ] Build a temporary complete target set before publication, or clear every partial set on failure.
- [ ] Make validity checks cover all required frame resources.

**Acceptance criteria**

- [ ] Failed creation cannot lead to null framebuffer or descriptor use.
- [ ] A subsequent successful retry recovers cleanly without leaks.
- [ ] Preserve Metal failure behavior.
