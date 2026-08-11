# Bug Review Report

## Findings

### 1. PTY winsize clamped below grid size → shell/grid desync
- **File / line:** `libs/draxul-terminal-process/src/unix_pty_process.cpp:248-249,613-614`; `libs/draxul-terminal-process/src/conpty_process.cpp:478-479,813-814`; callers resize grid without the same clamp in `libs/draxul-host/src/terminal_host_base.cpp:340-350`
- **Severity:** HIGH
- **What goes wrong:** Viewport sizing uses full `grid_size` (can exceed 320×200; `Grid` allows up to 10000). PTY/ConPTY size is hard-clamped to 320 cols / 200 rows. On a large window or small font, the grid is e.g. 400×220 while the child still has 320×200. Shell wrapping, cursor, and full-screen apps disagree with the rendered grid.
- **Suggested fix:** Clamp grid and PTY together (or raise/remove the PTY clamp and pass the true size both ways):
```cpp
const int cols = std::clamp(new_cols, 1, kMaxPtyCols);
const int rows = std::clamp(new_rows, 1, kMaxPtyRows);
core_.resize(cols, rows);
do_process_resize(cols, rows);
```

### 2. Server scrollback budget double-reserves on col resize → valid resizes fail
- **File / line:** `libs/draxul-server/src/server_terminal_runtime.cpp:443-462`
- **Severity:** HIGH
- **What goes wrong:** On column change the code temporarily reserves `previous_reservation + requested_reservation`. Near the global budget (`kServerMaxScrollbackCells`), that rejects resizes that fit after release—including shrinks. Example: one terminal holds the full budget (or many panes sum to the limit); any col change needs peak double allocation and fails, so the shared terminal cannot resize.
- **Suggested fix:** Reserve the final size (or only the delta), not the sum:
```cpp
if (!budget->replace_scrollback_reservation(previous_reservation, requested_reservation))
    return false;
reserved_scrollback_cells_ = requested_reservation;
// on failure: restore_scrollback_reservation(previous_reservation);
```

### 3. CSI CHT/CBT loops `n` times with unbounded `n` → UI hang
- **File / line:** `libs/draxul-terminal-core/src/terminal_core_csi.cpp:303-319`
- **Severity:** HIGH
- **What goes wrong:** `param_or` can return up to `INT_MAX` (e.g. `\e[2147483647I`). CHT/CBT iterate that many times on the main/pump thread. Local and server-owned terminals hang on malicious or buggy output.
- **Suggested fix:** Cap tab stops by columns (no large loops):
```cpp
const int n = std::min(param_or(params, 0, 1), std::max(1, grid_cols()));
// or compute final tab stop in O(1)
```

### 4. CSI cursor moves: signed overflow on `vt_.row/col + n`
- **File / line:** `libs/draxul-terminal-core/src/terminal_core_csi.cpp:250-265` (and similar `+ param_or` sites)
- **Severity:** MEDIUM
- **What goes wrong:** `vt_.col + param_or(...)` / `vt_.row + param_or(...)` can overflow `int` when the parameter is large (e.g. `INT_MAX`). Overflow is UB; typical wrap yields a large negative value, and `std::min(max_bound, negative)` leaves a negative cursor. Later cell ops mostly no-op, but cursor publishing/selection can misbehave until something reclamps.
- **Suggested fix:** Use saturated arithmetic before clamp:
```cpp
const int n = param_or(params, 0, 1);
const int max_c = std::max(0, grid_cols() - 1);
vt_.col = (n > max_c - vt_.col) ? max_c : vt_.col + n;
```

---

**Not reported (already tracked / no solid trigger):** ice-box signed index overflow (`kanban/ice-box/07 cursor-index-signed-overflow -bug.md`); pending macOS channel / SIGTERM items (marked fixed in cards); theoretical sun_path overflow (validated at `ControlServer` start).
