# 43 hostcontext-named-init -refactor

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

`libs/draxul-host/include/draxul/host.h` defines `HostContext` with four positional
constructor overloads (lines 78–117).  The struct has grown to include window, renderer, text
service, config, ownership, launch, and DPI state — far past the point where positional
construction is readable at call sites.

Example of a call site that is hard to audit:
```cpp
HostContext ctx{window_, renderer_, text_service_, config_, host_ownership, launch_opts, dpi};
```
Reviewers cannot verify that `host_ownership` and `dpi` are not swapped without reading the
header.

C++20 designated initializers provide a zero-cost fix:
```cpp
HostContext ctx{
    .window = window_,
    .renderer = renderer_,
    // ...
};
```

## Acceptance Criteria

- [x] `HostContext` is restructured so that designated initializer syntax compiles (all
      fields are public, no non-trivial constructors blocking aggregate initialization, or a
      named builder is provided).
- [x] All four positional constructor overloads are removed or replaced with one default
      aggregate constructor.
- [x] All `HostContext` construction call sites are updated to use named fields.
- [x] All existing tests pass.

## Implementation Plan

1. Read `libs/draxul-host/include/draxul/host.h:78–117` to understand the four overloads.
2. Confirm whether all fields have default values (needed for aggregate init).
3. Convert `HostContext` to a simple aggregate struct.
4. Update all construction sites — use Grep to find them: `grep -r "HostContext{" app/ libs/`.
5. Run `cmake --build build --target draxul draxul-tests && py do.py smoke`.

## Files Likely Touched

- `libs/draxul-host/include/draxul/host.h`
- `app/host_manager.cpp` and other `HostContext` construction sites
- Potentially `app/app.cpp`

## Interdependencies

- **Low risk, high readability gain** — can be done independently of other open WIs.
- Coordinate with WI 38 (`App::Deps`) if `App` construction of `HostContext` is also being
  changed; batch them in sequence to avoid merge conflicts.
