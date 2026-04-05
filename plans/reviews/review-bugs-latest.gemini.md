### Draxul Bug Report

#### 1. CRITICAL: Broken Windows Argument Quoting in ConPtyProcess
- **File and line**: `libs/draxul-host/src/conpty_process.cpp:15`
- **Severity**: CRITICAL
- **What goes wrong**: The `quote_windows_arg` implementation fails to escape trailing backslashes before the closing quote. If an argument ends with a backslash (e.g., `C:\foo\`), it is transformed into `"C:\foo\"`. Windows command-line parsing rules interpret the final `\"` as an escaped literal quote rather than the end of the argument, causing the rest of the command line to be consumed into a single corrupted argument or causing `CreateProcess` to fail. This was fixed in `nvim_process.cpp` but missed in this file.
- **Suggested fix**:
  ```cpp
  // ... after the loop ...
  quoted.append(backslashes, '\\');
  quoted.push_back('"');
  return quoted;
  ```

#### 2. HIGH: Data Race on Frame Requests and App Running State
- **File and line**: `app/app.h:141, 142`
- **Severity**: HIGH
- **What goes wrong**: `App::frame_requested_` and `App::running_` are raw `bool` fields. They are modified from the RPC reader thread (via `on_notification_available` -> `request_frame`) and the PTY reader thread, while being read and modified on the main thread in `pump_once` and `render_frame`. This is a formal data race (Undefined Behavior) and can lead to missed frames or hung shutdowns on some architectures.
- **Suggested fix**: Change types to `std::atomic<bool>`.

#### 3. HIGH: Data Loss in Unix PTY Reader Thread
- **File and line**: `libs/draxul-host/src/unix_pty_process.cpp:301`
- **Severity**: HIGH
- **What goes wrong**: In `UnixPtyProcess::reader_main`, the code checks for `POLLHUP` and `POLLERR` and breaks the loop *before* checking for `POLLIN`. If the PTY is closed but there is still unread data in the kernel buffer, `poll` returns `POLLIN | POLLHUP`. The current code will immediately `break`, losing the final chunk of terminal output (e.g., the last few lines of a command's output before it exits).
- **Suggested fix**: Process `POLLIN` first, and only `break` on `POLLHUP` if `POLLIN` is not set or `read` returns 0.

#### 4. HIGH: Memory Leak on File Drops
- **File and line**: `libs/draxul-window/src/sdl_event_translator.cpp:92`
- **Severity**: HIGH
- **What goes wrong**: `translate_file_drop` extracts `event.drop.data` into a `std::string` but does not free the original pointer. According to SDL3 specifications, the data pointer in an `SDL_EVENT_DROP_FILE` event is heap-allocated and must be freed by the application. This causes a memory leak every time a file is dragged into the window.
- **Suggested fix**: Call `SDL_free(event.drop.data)` after copying the string.

#### 5. MEDIUM: Unbounded Memory Growth in HighlightTable
- **File and line**: `libs/draxul-grid/include/draxul/attribute_cache.h:78`
- **Severity**: MEDIUM
- **What goes wrong**: When `AttributeCache::compact` is called, it reassigns highlight IDs starting from 1 and updates the `HighlightTable`. However, `HighlightTable::attrs_` is a `std::unordered_map` that is never cleared. Old attribute IDs that are no longer "live" (and were not overwritten by the new compaction range) persist in the map indefinitely. Over long sessions with many color changes (e.g., a terminal tailing logs with random colors), this map will grow without bound.
- **Suggested fix**: Add and call a `clear()` method on `HighlightTable` at the start of `AttributeCache::compact`.

#### 6. MEDIUM: Inefficient GPU Uploads due to Broken Dirty Tracking
- **File and line**: `libs/draxul-renderer/src/renderer_state.cpp:310`
- **Severity**: MEDIUM
- **What goes wrong**: `RendererState::mark_cell_dirty` attempts to track a contiguous dirty range using `dirty_cell_begin_` and `dirty_cell_end_`. However, `dirty_cell_begin_` is initialized to `0` in `clear_dirty()` and is only updated if `index < dirty_cell_begin_`. Since `index` is always `>= 0`, `dirty_cell_begin_` stays at `0` forever. This forces every GPU upload to start from the first cell of the grid, even if only the last line changed, wasting PCIe bandwidth.
- **Suggested fix**: Initialize `dirty_cell_begin_` to `std::numeric_limits<size_t>::max()` in `clear_dirty()`.

#### 7. MEDIUM: Scrollback Buffer Wiped on Window Resize
- **File and line**: `libs/draxul-host/src/scrollback_buffer.cpp:22`
- **Severity**: MEDIUM
- **What goes wrong**: `ScrollbackBuffer::resize` clears the entire `storage_` vector and resets `count_` and `write_head_` to zero. This means that simply resizing the window causes the user to lose their entire terminal scrollback history. While not a crash, this is incorrect behavior for a terminal emulator.
- **Suggested fix**: Implement a "best-effort" reflow or at least preserve the old data by copying it into the new storage with the new stride.
