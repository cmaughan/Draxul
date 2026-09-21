# Check SDL3 event-registration failure correctly

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-window/src/sdl_window.cpp:175` checks for `Uint32(-1)`, but [SDL3 returns zero on failure](https://wiki.libsdl.org/SDL3/SDL_RegisterEvents). Exhausted event registration therefore leaves unusable wake/file-dialog event IDs while initialization appears successful.

**Investigation**

- [ ] Exercise failed allocation of the two application event IDs.

**Fix strategy**

- [x] Reject a zero result and unwind initialization through the existing error path.

**Acceptance criteria**

- [x] Registration failure reports an actionable initialization error.
- [x] Successful registration provides distinct working wake and file-dialog events.
- [ ] Verify both platform startup paths.
