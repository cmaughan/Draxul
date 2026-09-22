# Delete the Metal NanoVG context exactly once

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-nanovg/backend/src/nanovg_mtl.mm:1072` deletes the backend during NanoVG cleanup, and `:1105` deletes it again after initialization failure. The pinned [NanoVG implementation](https://raw.githubusercontent.com/memononen/nanovg/ce3bf745eb2d2dbc14a50bf2446783f691ac4353/src/nanovg.c) invokes that cleanup after renderer or atlas initialization fails.

**Investigation**

- [x] Inject renderer, atlas, and initial NanoVG allocation failures.

**Fix strategy**

- [x] Give backend allocation ownership to one layer across successful creation, failed creation, and normal deletion.
- [x] Preserve cleanup when NanoVG fails before installing its callbacks.

**Implementation note (2026-09-22):** A temporary `unique_ptr` owns the Metal
backend through `nvgCreateInternal`; its cleanup callback releases that temporary
owner before deleting the backend. On success ownership transfers to NanoVG for
normal teardown. Thread-local fault injection now drives failures before NanoVG
installs its callbacks, from renderer initialization, and from initial font-atlas
creation. The focused target built successfully; both CTest shards passed on macOS,
and the live Metal ownership filter passed 24 assertions across two test cases.
Each failure records one backend allocation and one deletion, and the normal path
records one callback-driven deletion after successful creation. ASan is retired
from the project, so no sanitizer result is claimed.
The final Debug validation passed all 45 unit CTest entries, the same-cache
startup smoke, and all five default Metal render comparisons.

**Acceptance criteria**

- [x] Every injected failure frees the backend once, with no double-free or retained owner.
- [x] Normal initialization and teardown pass the live Metal ownership gate (ASan retired/N/A).
