# 17 log-would-emit-atomic -refactor

**Priority:** LOW
**Type:** Refactor (hot-path performance)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

The logging system acquires a `std::mutex` on every call to `log_would_emit()`. This function is likely called from hot rendering paths (every frame, potentially multiple times) to gate conditional logging. In the common case (logging is disabled for that level), the mutex is acquired and released with no other work done. A `std::atomic<LogLevel>` for the active level comparison would make the common "disabled" path lock-free with no behavior change.

---

## Implementation Plan

- [ ] Find the `log_would_emit()` implementation (search for `log_would_emit` in `libs/` or `app/`).
- [ ] Read the logging system to understand how the active log level is stored and mutated. Identify all write sites (typically: startup config, runtime level change command).
- [ ] Replace the level variable with `std::atomic<int>` (or `std::atomic<LogLevel>` if `LogLevel` is an `int`-backed enum with no padding).
- [ ] In `log_would_emit()`: use `level_.load(std::memory_order_relaxed)` for the comparison. No mutex needed for the read-only fast path.
- [ ] In any write site (changing the active log level): use `level_.store(..., std::memory_order_relaxed)` — exact ordering is not critical since log level changes are infrequent and not synchronized with application logic.
- [ ] Keep the mutex (if any) for the write path to `log_printf()` itself (output serialisation) — this is not part of this change.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run `clang-format`.

---

## Acceptance

- `log_would_emit()` does not acquire a mutex.
- Log level reads are lock-free.
- Behavior is identical — no output changes under normal usage.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
