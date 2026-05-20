# WI 106 — bmp-bare-path-throws

**Type:** bug  
**Priority:** 2 (uncaught exception terminates process on screenshot / render-test capture)  
**Source:** review-bugs-consensus.md §M3 [GPT]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`write_bmp_rgba()` (`libs/draxul-types/src/bmp.cpp:46`) unconditionally calls:

```cpp
std::filesystem::create_directories(path.parent_path());
```

When `path` is a bare filename like `out.bmp`, `path.parent_path()` returns an empty path
(`std::filesystem::path{}`). `create_directories("")` throws `std::filesystem::filesystem_error` on
most platforms (including macOS and Windows). This call is not wrapped in a `try/catch` and the
exception propagates uncaught through the render-test or screenshot capture path.

**Trigger:** `--screenshot out.bmp` or any render-test output path that does not include a directory
component.

---

## Investigation

- [ ] Read `libs/draxul-types/src/bmp.cpp:40–60` — confirm the `create_directories` call and that
  there is no surrounding try/catch.
- [ ] Check call sites of `write_bmp_rgba` — identify which code paths can pass a bare filename.
- [ ] Verify on macOS: `std::filesystem::create_directories("")` — confirm it throws (expected per
  the standard: `""` is an empty path, `create_directories` requires a non-empty path).

---

## Fix Strategy

Guard the call:

```cpp
if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
```

- [ ] Apply the guard.
- [ ] Consider also propagating the `filesystem_error` if `create_directories` fails on a
  non-empty path (currently it throws and the BMP is never written; logging would be cleaner).
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`
- [ ] Manually verify: `./build/draxul.app/Contents/MacOS/draxul --screenshot out.bmp` (or
  equivalent) completes without throwing.

---

## Acceptance Criteria

- [ ] `write_bmp_rgba` with a bare filename does not throw.
- [ ] `write_bmp_rgba` with a path that includes a directory still creates the directory.
- [ ] Smoke test passes.

---

## Interdependencies

None — isolated change in `bmp.cpp`.
