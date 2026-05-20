# WI 117 — MetalRenderer Dual ObjC/C++ Header Layout Hazard

**Type:** Bug  
**Severity:** Medium (silent memory corruption if layout diverges)  
**Source:** Claude review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`libs/draxul-renderer/src/metal/metal_renderer.h` (approx L86–112) contains `#ifdef __OBJC__` sections that declare Metal object members using `ObjCRef<id<MTLDevice>>` and similar types. In plain C++ TUs (non-ObjC++), the same members are declared as `void*`.

If a Metal handle is added to the ObjC++ section but forgotten in the C++ fallback block (or vice versa), the struct layout silently diverges between translation units. A C++ TU then reads Metal state at wrong offsets — classic silent memory corruption, no compiler warning.

Claude flagged this as an active hazard: "If a new Metal handle is added to the ObjC section but not the C++ fallback (or vice versa), struct layout silently differs between translation units, causing memory corruption."

---

## Investigation Steps

- [x] Open `libs/draxul-renderer/src/metal/metal_renderer.h`
- [x] Count the `#ifdef __OBJC__` vs bare-C++ member declarations
- [x] Check which TUs include this header as plain C++ (not ObjC++) — these are at risk
- [ ] Run with AddressSanitizer to check for any current corruption

---

## Fix Strategy

**Option A — Pimpl (preferred):** Move all ObjC handles into a forward-declared `MetalRendererImpl` struct defined only in `.mm` files. The `.h` header holds only a `std::unique_ptr<MetalRendererImpl>` (or a raw pointer with a custom deleter). Plain C++ TUs see a stable layout with no ObjC types.

**Option B — Opaque pointer:** Replace each `#ifdef __OBJC__` block with a single `void* impl_` and cast in `.mm` files. Simpler but less type-safe.

Either option eliminates the layout bifurcation.

---

## Acceptance Criteria

- [x] `metal_renderer.h` contains no `#ifdef __OBJC__` member declarations.
- [x] Build succeeds for both ObjC++ and plain C++ TUs that include the header.
- [ ] ASan build (`mac-asan` preset) shows no new errors.
- [ ] CI green.

## Status

Fixed by making `metal_renderer.h` ObjC++-only and routing the plain-C++
factory through a new free function.

Changes:
- Added `libs/draxul-renderer/src/metal/metal_renderer_factory.h` declaring
  `create_metal_renderer(int, RendererOptions)` which returns
  `std::unique_ptr<IGridRenderer>`. This header is plain-C++ safe.
- `libs/draxul-renderer/src/metal/metal_renderer.h`: removed both
  `#ifdef __OBJC__` / `#else void*` member blocks. The header now has a
  single set of `ObjCRef<...>`-typed members and a `#error` guard that
  prevents accidental inclusion from plain C++ TUs.
- `libs/draxul-renderer/src/metal/metal_renderer.mm`: defines
  `create_metal_renderer()` next to the `MetalRenderer` constructor.
- `libs/draxul-renderer/src/renderer_factory.cpp`: now includes
  `metal_renderer_factory.h` and calls `create_metal_renderer(...)`
  instead of `std::make_unique<MetalRenderer>(...)`. This TU no longer
  sees the `MetalRenderer` class definition at all, so the dual-layout
  hazard is structurally eliminated.

Build: `cmake --build build --target draxul draxul-tests` succeeds.
ASan preset not re-run as part of this change (noted as unchecked).

---

## Interdependencies

- Should be done before any expansion of the Metal renderer (new passes, new resources).
- Subagent recommended: a single focused agent can handle this as a pure refactor with no behaviour change.
