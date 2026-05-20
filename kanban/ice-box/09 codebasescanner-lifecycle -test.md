# Test: CodebaseScanner Lifecycle Robustness

**Type:** test
**Priority:** 9
**Source:** Gemini review

## Problem

`CodebaseScanner` (in `libs/draxul-treesitter/` or `libs/draxul-megacity/`) runs a background thread and is documented with a "start once" lifecycle. There is currently only one focused test; the following critical scenarios are not exercised:

1. **Restart safety**: `start → stop → start` should work without data races or crashes.
2. **Directory skipping**: hidden directories (`.git`, `.build`, `node_modules`), build artifact directories, and Objective-C (`.mm`) sources should be skipped per the scanner's documented behaviour.
3. **Parse-error bounding**: if many files contain parse errors, the error count should be capped and a completed snapshot still delivered (the scanner must not stall indefinitely on errors).

**Note:** Do this alongside `10 citydb-reconcile-robustness -test` and `12 megacity-degraded-init -test` — a single agent can share fixture setup across all three.

## Investigation steps

- [ ] Read `libs/draxul-treesitter/include/draxul/treesitter.h` and `.cpp` — locate `start()`, `stop()`, and the background thread logic.
- [ ] Find `tests/treesitter_tests.cpp` — check what is already tested.
- [ ] Identify the configuration for which directories and file types are skipped.
- [ ] Find how completed snapshots are signalled (callback? condition variable? queue?).

## Test design

Add to `tests/treesitter_tests.cpp` or create `tests/codebasescanner_tests.cpp`.

### Restart test

- [ ] Create a `CodebaseScanner` pointing at a temp directory with a few source files.
- [ ] Call `start()`, wait for a snapshot, call `stop()`.
- [ ] Call `start()` again, wait for a new snapshot.
- [ ] Assert: the second snapshot is valid and no data race (run under ASan/TSan).

### Directory skip test

- [ ] Create a temp directory tree with source files in:
  - `.git/` (should be skipped)
  - `node_modules/` (should be skipped)
  - `build/` (should be skipped, if configured)
  - `src/` (should be scanned)
- [ ] Assert: snapshot contains only symbols from `src/`, not from hidden/build dirs.

### Parse-error cap test

- [ ] Create `N` files where `N > kMaxParseErrors` (check the constant), each containing invalid C++.
- [ ] Start the scanner and wait for snapshot completion.
- [ ] Assert: snapshot is delivered (not stalled), error count is capped, valid files are still parsed.

## Acceptance criteria

- [ ] All three scenarios pass under ASan.
- [ ] Tests are part of `draxul-tests` and do not require a real Neovim instance.

## Interdependencies

- **`10 citydb-reconcile-robustness -test`** and **`12 megacity-degraded-init -test`**: share fixture setup, do in same agent pass.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
