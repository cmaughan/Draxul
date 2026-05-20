# WI 91 — sdl-file-drop-memory-leak

**Type:** bug  
**Priority:** 1 (memory leak on every file drop)  
**Source:** review-bugs-consensus.md §H5 [Gemini]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`translate_file_drop()` in `libs/draxul-window/src/sdl_event_translator.cpp:107–113` copies `event.drop.data` into a `std::string` but never calls `SDL_free(event.drop.data)`. Per SDL3 documentation, `SDL_EVENT_DROP_FILE` supplies a heap-allocated string that the application must free. Every file dragged into the window leaks the path string.

---

## Investigation

- [ ] Read `libs/draxul-window/src/sdl_event_translator.cpp:107–115` — confirm `SDL_free` is absent.
- [ ] Check the SDL3 documentation or headers for `SDL_EVENT_DROP_FILE` — confirm the application is responsible for freeing `event.drop.data`.
- [ ] Check whether `SDL_EVENT_DROP_TEXT` is handled anywhere and whether it has the same requirement.

---

## Fix Strategy

- [ ] Free `event.drop.data` after copying it:
  ```cpp
  std::optional<std::string> translate_file_drop(const SDL_Event& event)
  {
      if (event.type != SDL_EVENT_DROP_FILE || !event.drop.data)
          return std::nullopt;
      std::string path(event.drop.data);
      SDL_free(event.drop.data);
      return path;
  }
  ```
- [ ] Apply the same fix to any `SDL_EVENT_DROP_TEXT` handler if present.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `SDL_free` is called on `event.drop.data` after it is copied.
- [ ] No memory leak detected by ASan/Leaks when a file is dragged onto the window.
- [ ] Smoke test passes.
