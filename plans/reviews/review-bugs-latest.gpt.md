Current-tree findings below exclude issues already tracked under `plans/`.

1. **CRITICAL** `[libs/draxul-gui/src/palette_renderer.cpp#L165](/Users/cmaughan/dev/Draxul/libs/draxul-gui/src/palette_renderer.cpp#L165)`, `[libs/draxul-gui/src/palette_renderer.cpp#L171](/Users/cmaughan/dev/Draxul/libs/draxul-gui/src/palette_renderer.cpp#L171)`, `[app/command_palette_host.cpp#L90](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L90)`, `[app/command_palette_host.cpp#L96](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L96)`  
   **What goes wrong:** `CommandPaletteHost` computes raw `grid_rows = pixel_h_ / ch` and passes that raw value into `palette_.view_state()`. If the palette viewport is only 1 text row tall, `render_palette()` still runs, computes `sep_local_row = layout.rows - 2`, and then writes `at(c, -1)`. That negative row is cast to `size_t` inside the indexer and writes before the `grid` vector, so opening or resizing the palette in a very short window can corrupt heap memory or crash immediately.  
   **Suggested fix:** Clamp the dimensions before building the palette view state, or bail out early in `render_palette()` when `layout.rows < 2`:
   ```cpp
   if (state.grid_rows < 2) return {};
   ```

2. **CRITICAL** `[libs/draxul-gui/src/palette_renderer.cpp#L181](/Users/cmaughan/dev/Draxul/libs/draxul-gui/src/palette_renderer.cpp#L181)`, `[libs/draxul-gui/src/palette_renderer.cpp#L187](/Users/cmaughan/dev/Draxul/libs/draxul-gui/src/palette_renderer.cpp#L187)`, `[libs/draxul-gui/src/palette_renderer.cpp#L199](/Users/cmaughan/dev/Draxul/libs/draxul-gui/src/palette_renderer.cpp#L199)`, `[app/command_palette_host.cpp#L90](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L90)`  
   **What goes wrong:** The input row assumes there is room for at least the prompt and cursor. With a 1-column palette (`grid_cols == 1`), `prompt = at(pad, input_local_row)` writes column 1 into a width-1 grid. For even slightly wider but still tiny widths, `query_max` becomes negative and `cursor_local_col` can go negative; the `< layout.cols - pad` check has no lower bound, so `at(-1, row)` is also possible. A very narrow window can therefore crash or scribble memory as soon as the palette opens.  
   **Suggested fix:** Guard small widths before any writes and clamp cursor/prompt columns:
   ```cpp
   if (state.grid_cols < 2) return {};
   const int cursor_local_col = std::clamp(query_col0 + query_len, 0, layout.cols - 1);
   ```

3. **HIGH** `[app/main.cpp#L45](/Users/cmaughan/dev/Draxul/app/main.cpp#L45)`, `[app/main.cpp#L51](/Users/cmaughan/dev/Draxul/app/main.cpp#L51)`, `[app/main.cpp#L52](/Users/cmaughan/dev/Draxul/app/main.cpp#L52)`  
   **What goes wrong:** On Windows, the first `WideCharToMultiByte()` call returns a size that includes the terminating NUL because `cchWideChar == -1`. The code then allocates `size - 1` bytes but still converts with `cchWideChar == -1`, so the second conversion call is one byte short and can fail with `ERROR_INSUFFICIENT_BUFFER`. The failure is unchecked, so non-empty command-line arguments can become empty strings and flags like `--host`, `--command`, `--log-file`, and screenshot options are silently misparsed or ignored.  
   **Suggested fix:** Allocate the full returned size, check the second call, then drop the NUL:
   ```cpp
   std::string converted(size, '\0');
   if (WideCharToMultiByte(..., converted.data(), size, ...) == 0) { /* handle error */ }
   converted.pop_back();
   ```

4. **HIGH** `[libs/draxul-host/src/unix_pty_process.cpp#L39](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/unix_pty_process.cpp#L39)`, `[libs/draxul-host/src/unix_pty_process.cpp#L72](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/unix_pty_process.cpp#L72)`, `[libs/draxul-host/src/unix_pty_process.cpp#L105](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/unix_pty_process.cpp#L105)`  
   **What goes wrong:** `spawn()` sets `SIGPIPE` to `SIG_IGN` process-wide before `fork()`, and the child never restores the default disposition before `execvp()`. Every shell/program launched inside the PTY therefore inherits ignored `SIGPIPE`. That changes normal Unix pipeline behavior: commands that should die on broken pipe can instead keep running or print `EPIPE` errors. A concrete repro is a pipeline like `yes | head -n1`, which should terminate cleanly via `SIGPIPE`.  
   **Suggested fix:** Restore the default signal disposition in the child before `execvp()`:
   ```cpp
   signal(SIGPIPE, SIG_DFL);
   ```
   Prefer `sigaction()` over `signal()` if you want explicit, portable disposition handling.