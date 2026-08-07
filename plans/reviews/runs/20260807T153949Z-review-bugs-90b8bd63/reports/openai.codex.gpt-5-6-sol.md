## Findings

1. **HIGH** — `libs/draxul-terminal-process/src/conpty_process.cpp:148,546`  
   Every ConPTY launch scans all desktop console windows for eight seconds and hides any not present in the initial snapshot. An unrelated `cmd.exe` opened during that interval is hidden; the scan continues even if `CreateProcessW` fails.  
   **Fix:** Remove the global scan, or start it after successful creation and restrict it to the spawned process/conhost tree.

2. **HIGH** — `libs/draxul-types/src/log.cpp:320,337`  
   `log_message()` invokes the configured sink while holding the logger mutex. A sink that logs recursively, clears itself, or reconfigures logging deadlocks immediately.  
   **Fix:** Copy the sink while locked, release the mutex, then invoke the copy.

3. **HIGH** — `libs/draxul-host/src/local_terminal_host.cpp:63,140,763,774`  
   Scrollback and resize restoration rebuild cells through `Grid::set_cell()`, which clears `hyperlink_id` at `libs/draxul-grid/src/grid.cpp:270`. An OSC 8 link stops working after scrolling back or resizing the terminal.  
   **Fix:** Restore the complete `Cell`, or call `set_cell_hyperlink_id()` after every restored cell.

4. **MEDIUM** — `scripts/update_screenshot.py:37`  
   On macOS, `draxul_path()` returns `build/draxul`, while the configured target is `build/draxul.app/Contents/MacOS/draxul`. The screenshot updater therefore exits with “Built app not found” after a successful standard build.  
   **Fix:** Add a `sys.platform == "darwin"` app-bundle path.

5. **MEDIUM** — `scripts/store_logs.sh:10`  
   The macOS log helper launches nonexistent `build/draxul` instead of the app-bundle executable, so it cannot collect logs from a normal macOS build.  
   **Fix:** Launch `build/draxul.app/Contents/MacOS/draxul` on Darwin.

6. **MEDIUM** — `tests/startup_rollback_tests.cpp:131`  
   The window-error assertion contains `|| true`, so it passes even when `init_error()` no longer identifies the window failure.  
   **Fix:** Remove `|| true` and assert the expected error text directly.
