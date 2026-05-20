# 09 ImGui Bottom Panel Integration

## Why This Exists

The current debug overlay is rendered as fake grid text, which keeps it cheap but makes it an awkward place to grow richer tooling.

The next UI-facing step is to splice in Dear ImGui cleanly so Draxul can host a real diagnostics panel without entangling `app/` with backend-private renderer code.

## Goal

Replace the grid-text debug overlay with a bottom-aligned ImGui panel that works with both Vulkan and Metal, shrinks the terminal viewport while visible, and keeps backend-specific ImGui plumbing private to `draxul-renderer`.

## Implementation Plan

- [x] Add a dedicated UI integration library.
  - [x] create `draxul-ui` for panel state, layout, input adaptation, and widget construction
  - [x] keep backend-specific ImGui setup private to `draxul-renderer`
- [x] Introduce terminal-vs-panel layout.
  - [x] compute a bottom panel rect from window pixels and cell metrics
  - [x] shrink grid row calculation and resize requests when the panel is visible
  - [x] keep text-input area and terminal mouse routing constrained to the terminal region
- [x] Add a renderer-owned ImGui pass.
  - [x] add a narrow renderer seam for ImGui frame prep and draw-data submission
  - [x] record ImGui after terminal content and before present/capture
  - [x] validate the Metal backend path on macOS
- [x] Port the current debug information into the new panel.
  - [x] reuse the same metrics currently shown by the debug overlay
  - [x] remove the old grid-cell debug overlay path once the panel is live
- [x] Follow up on panel ergonomics once the basic integration is stable.
  - [x] allow future controls or tabs without reopening renderer internals
  - [x] add keyboard-focused input capture instead of the current mouse-first read-only path
  - [x] add a visible-panel render scenario so the bottom panel itself gets snapshot coverage

## Tests

- [x] add focused layout tests for visible/hidden panel geometry
- [x] run `draxul-tests`
- [x] run `draxul-app-smoke`
- [x] run `ctest --test-dir build --build-config Release --output-on-failure`

## Suggested Slice Order

1. layout and app routing
2. renderer ImGui seam
3. backend integration
4. debug panel port and cleanup

## Sub-Agent Split

- one agent on app/layout/input routing
- one agent on Vulkan/Metal backend ImGui plumbing
- one agent on panel widget content and tests
