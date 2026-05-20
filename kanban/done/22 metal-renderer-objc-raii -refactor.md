# 22 Metal Renderer ObjC RAII Wrapper

## Why This Exists

`MetalRenderer` stores every Metal API object (`MTLDevice`, `MTLCommandQueue`, `CAMetalLayer`, etc.)
as `void*` with manual `__bridge_retained` / `__bridge_transfer` casts throughout `initialize()`,
`shutdown()`, and `set_grid_size()`. A mismatched retain/release pair causes a memory leak or
double-free with no compile-time safety and no sanitiser visibility.

Modern idiomatic Objective-C++ would store these as `id<Protocol>` or use ARC, but since the
renderer mixes C++ and Obj-C, a small RAII wrapper `ObjCRef<T>` can give explicit, safe ownership
semantics without requiring full ARC.

**Source:** `libs/draxul-renderer/src/metal/metal_renderer.mm`.
**Raised by:** Claude.

## Goal

Replace `void*` Metal object handles with a thin `ObjCRef<T>` RAII wrapper that:
- Calls `CFBridgingRetain` on construction.
- Calls `CFBridgingRelease` (or `(__bridge_transfer T)` to ARC) on destruction.
- Provides `T get()` to get the underlying typed pointer for passing to Metal APIs.
- Is non-copyable, move-only.

## Implementation Plan

- [x] Read `libs/draxul-renderer/src/metal/metal_renderer.mm` to inventory all `void*` handles and their retain/release sites.
- [x] Create a header `libs/draxul-renderer/src/metal/objc_ref.h` with the `ObjCRef<T>` template.
  - Template parameter `T` is an Obj-C type (e.g., `id<MTLDevice>`).
  - Constructor takes `T` and calls `CFBridgingRetain`.
  - Destructor calls `CFBridgingRelease`.
  - `T get() const` returns the typed pointer.
  - Delete copy constructor/assignment; provide move.
- [x] Replace each `void*` member in `MetalRenderer` with `ObjCRef<appropriate_type>`.
- [x] Remove the manual `__bridge_retained`/`__bridge_transfer` calls at each usage site.
- [x] Build on macOS only (Metal is macOS-exclusive). Verify the app starts and renders correctly.
- [x] Run `ctest --test-dir build` on macOS.

## Notes

This is a macOS-only change. Do not touch any Windows/Vulkan code.
The `ObjCRef` header can live in the `metal/` private source directory; it does not need to be
exported as a public header.

## Sub-Agent Split

Single agent. macOS-specific; requires access to a macOS build environment.
