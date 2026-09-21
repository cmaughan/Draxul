# Delete the Metal NanoVG context exactly once

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-nanovg/backend/src/nanovg_mtl.mm:1072` deletes the backend during NanoVG cleanup, and `:1105` deletes it again after initialization failure. The pinned [NanoVG implementation](https://raw.githubusercontent.com/memononen/nanovg/ce3bf745eb2d2dbc14a50bf2446783f691ac4353/src/nanovg.c) invokes that cleanup after renderer or atlas initialization fails.

**Investigation**

- [ ] Inject renderer, atlas, and initial NanoVG allocation failures.

**Fix strategy**

- [x] Give backend allocation ownership to one layer across successful creation, failed creation, and normal deletion.
- [x] Preserve cleanup when NanoVG fails before installing its callbacks.

**Implementation note (2026-09-21):** A temporary `unique_ptr` owns the Metal
backend through `nvgCreateInternal`; its cleanup callback releases that temporary
owner before deleting the backend. On success ownership transfers to NanoVG for
normal teardown. The Objective-C++ backend compiled and focused NanoVG/Vulkan
source-contract tests passed on macOS (75 cases, 635 assertions). Failure
injection and sanitizer validation remain pending.

**Acceptance criteria**

- [ ] Every failure frees resources once, with no double-free or leak.
- [ ] Normal initialization and teardown remain valid under sanitizers.
