# 16 cmake-apple-branch-cleanup — Refactor

## Summary

The top-level `CMakeLists.txt` contains an `if(APPLE) … else() … endif()` block around `add_executable(draxul …)` (approximately lines 101-117) where both the `if(APPLE)` branch and the `else()` branch list exactly the same set of source files. The conditional is dead code.

This misleads contributors who want to add a platform-specific source file: they see the branch structure and conclude they should add their platform file to only one branch, when in reality both branches always produce the same target. Platform-specific sources are handled elsewhere (inside the renderer library's CMakeLists.txt, via per-platform `CMakeLists.txt` in `libs/draxul-renderer/src/`, or via `#ifdef` in source).

## Steps

- [x] 1. Read the top-level `CMakeLists.txt` from line 90 to line 130 (or the full file if short). Identify the exact `if(APPLE) … else() … endif()` block around `add_executable(draxul …)`.
- [x] 2. Confirm that both branches are genuinely identical — copy the file list from both branches and compare them character by character. If there are ANY differences, do NOT collapse them; instead note the difference as a comment and file a follow-up.
- [x] 3. If both branches are identical: remove the `if(APPLE) … else() … endif()` wrapping and replace with a single unconditional `add_executable(draxul …)`.
- [x] 4. Add a comment above or below `add_executable` explaining where platform-specific wiring actually happens.
- [x] 5. Search the rest of `CMakeLists.txt` for any other dead `if(APPLE)/else` blocks with identical branches.
- [x] 6. Build on macOS: `cmake --build build --target draxul`. Confirm the executable target still builds and links.
- [x] 7. If a Windows CI environment is available, also build there. Otherwise, note this as a CI-validation step.
- [x] 8. Run: `ctest --test-dir build -R draxul-tests` on macOS. All tests must pass.
- [x] 9. Run the smoke test: `py do.py smoke`.
- [x] 10. Run clang-format — note this is a CMake file, not a C++ file, so clang-format does not apply. No formatting step needed.
- [x] 11. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- No `if(APPLE)/else` block with identical source lists remains in `CMakeLists.txt`.
- A comment explains where genuine platform-specific source selection happens.
- `cmake --build build --target draxul` succeeds on macOS.
- All tests pass.

*Authored by: claude-sonnet-4-6*
