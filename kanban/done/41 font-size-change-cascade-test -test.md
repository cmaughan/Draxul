# 41 Font Size Change Cascade Test

## Why This Exists

Changing the font size triggers a sequence: atlas reset → font metrics recalculate → grid dirty-mark → `nvim_ui_try_resize` RPC → layout recalculate. This sequence has no automated test coverage. A regression in any step (e.g., atlas reset before grid dirty-mark, or resize RPC sent with stale dimensions) produces visual corruption that only the render snapshot tests might catch — after significant human effort.

Identified by: **Claude** (tests #2), **GPT** (tests #2).

## Goal

Add an integration test that drives font size changes through the app-support layer and verifies the ordering: atlas cleared, grid marked dirty, and resize RPC issued with correct dimensions.

## Implementation Plan

- [x] Read `app/app.cpp` for `change_font_size()` and related methods.
- [x] Read `libs/draxul-app-support/` for any existing test seams around font size.
- [x] Identify what is testable without a full window + renderer: `GridRenderingPipeline` atlas reset, `Grid` dirty-marking, `UiRequestWorker` resize dispatch.
- [x] Write `tests/font_size_tests.cpp`:
  - Construct a `GridRenderingPipeline` with a fake atlas and fake renderer.
  - Call a `change_font_size` equivalent or trigger the cascade manually.
  - Assert atlas was reset (reset counter incremented).
  - Assert grid is fully dirty after reset.
  - Assert `UiRequestWorker` queued a resize request with new dimensions.
- [x] Wire into `tests/CMakeLists.txt`.
- [x] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. May need a sub-agent to explore test seams in `GridRenderingPipeline` if the API is not yet exposed.
