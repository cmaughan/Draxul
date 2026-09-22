# Check SDL3 event-registration failure correctly

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-window/src/sdl_window.cpp:175` checks for `Uint32(-1)`, but [SDL3 returns zero on failure](https://wiki.libsdl.org/SDL3/SDL_RegisterEvents). Exhausted event registration therefore leaves unusable wake/file-dialog event IDs while initialization appears successful.

**Investigation**

- [x] Exercise failed allocation of the two application event IDs through an
      injectable adapter without exhausting SDL's process-global event range.

**Fix strategy**

- [x] Reject a zero result and unwind initialization through the existing error path.

**Acceptance criteria**

- [x] Registration failure reports an actionable initialization error.
- [x] Successful registration provides distinct working wake and file-dialog events.
- [x] Verify the macOS startup path through the core aggregate and same-cache
      Metal smoke.
- [x] Verify the Windows/Vulkan startup path.

**Validation (2026-09-22):** the injected failure/success cases passed as part
of the 23/23 core aggregate, followed by a successful same-cache Metal smoke.

## Windows closeout (2026-09-22)

The injected SDL registration cases passed in the final 48/48 products
aggregate, followed by a successful same-cache Vulkan smoke.
