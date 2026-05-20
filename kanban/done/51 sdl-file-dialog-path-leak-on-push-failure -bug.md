# Bug: Heap-allocated path leaked when SDL_PushEvent fails in file dialog

**Severity**: MEDIUM
**File**: `libs/draxul-window/src/sdl_file_dialog.cpp:34`
**Source**: review-bugs-consensus.md (M2)

## Description

In the file-dialog callback, `path` is heap-allocated at line 24 via `make_unique<std::string>(...).release()`. The raw pointer is stored in `SDL_UserEvent::data1` and ownership is intended to transfer to the event queue. However, if `SDL_PushEvent` at line 34 fails (queue full, SDL shutting down), the pointer is never freed — neither here nor in `handle_file_dialog_event` which will never see it.

## Trigger Scenario

1. User selects a file while SDL's event queue is full or SDL is shutting down.
2. `SDL_PushEvent` returns 0 (failure).
3. The `std::string` allocation at `path` leaks.

## Fix Strategy

- [x] Keep the string in a `unique_ptr` through the push attempt:
  ```cpp
  auto path = std::make_unique<std::string>(filelist[0]);
  // ... build SDL_UserEvent with path.get() in data1 ...
  if (SDL_PushEvent(&ev) > 0)
      path.release(); // transferred to event queue
  // else: path is freed by unique_ptr destructor
  ```
- [x] Fix C3 (use-after-free) in the same session — see work item 50

## Acceptance Criteria

- [x] No memory leak when `SDL_PushEvent` fails (verified with LeakSanitizer or manual failure injection)
- [x] Normal file-selection path still correctly frees `path` in `handle_file_dialog_event`
