# 36 param-or-lambda-dedup -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

The same `param_or` helper lambda is independently defined inside five functions in
`libs/draxul-host/src/terminal_host_base_csi.cpp`:

- `csi_cursor_move`
- `csi_erase`
- `csi_scroll`
- `csi_insert_delete`
- `csi_margins`

Each copy captures `params` and returns a default value when the parameter is absent or zero.
Because each is an independent copy, they can drift independently — for example, one copy
might add a clamp while another does not.

## Acceptance Criteria

- [x] `param_or` is extracted to a free function (or `static` helper) at the top of
      `terminal_host_base_csi.cpp` (or into a companion `terminal_host_base_csi_helpers.h`
      if it needs to be shared with other TUs).
- [x] All five call sites use the extracted function.
- [x] The function signature is exactly equivalent to the existing lambda (same return type,
      same default-value semantics).
- [x] All existing VT parser tests pass unchanged.

## Implementation Plan

1. Read `libs/draxul-host/src/terminal_host_base_csi.cpp` and confirm all five copies are
   identical (or identify any subtle differences that must be reconciled first).
2. Extract to a `static` free function above the first use site.
3. Replace all five lambda definitions with calls to the free function.
4. Run `cmake --build build --target draxul-tests && ctest -R terminal_vt`.

## Files Likely Touched

- `libs/draxul-host/src/terminal_host_base_csi.cpp` only

## Interdependencies

- **WI 41** (`cmake-configure-depends`) — independent.
- Anyone adding a new CSI sequence should read this file after this lands to use the
  extracted helper.
- **Sub-agent recommendation**: can be done in the same agent pass as WI 41 (both are
  mechanical, low-blast-radius changes).
