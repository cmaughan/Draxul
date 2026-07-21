# Bounded Kitty terminal graphics support

**Type:** feature
**Priority:** 47
**Raised by:** GPT/Codex

## Scope decision

Implement one bounded protocol first: Kitty graphics with PNG/raw RGBA transmission, placement, scrolling, and deletion. Defer Sixel/iTerm2 until the image model proves reusable.

## Implementation plan

- [ ] Write a protocol subset/design note with supported commands, limits, responses, and unsupported behavior.
- [ ] Extend VT/OSC/APC parsing with streaming base64 decode, hard encoded/decoded byte limits, dimension/pixel limits, and timeout/reset behavior.
- [ ] Decode images off the main thread into validated RGBA buffers; use a pinned cross-platform decoder and cancellation/generation checks.
- [ ] Add a terminal image store keyed by protocol id with bounded memory/LRU and explicit delete semantics.
- [ ] Represent placements separately from grid cells so scroll/resize/alternate-screen behavior is deterministic.
- [ ] Add one backend-neutral image render pass and implement Vulkan/Metal texture upload/composition with correct clipping and cell z-order.
- [ ] Define clipboard/security behavior: never execute payloads or read arbitrary host paths unless an explicitly supported safe command permits it.
- [ ] Add config enable/limits and document the supported subset.

## Tests and acceptance

- [ ] Parser tests cover chunking, invalid base64, oversized/malformed images, delete/place, ids, alternate screen, scroll, resize, and fuzz input.
- [ ] Renderer tests cover clipping, alpha, lifetime, eviction, and device failure on both backends.
- [ ] A standard Kitty-protocol sample renders identically on Windows and macOS within snapshot tolerance.
- [ ] Memory and frame time remain bounded under hostile input.

## Dependencies and parallelism

Benefits from items 19, 28, and 35. Split parser/store and renderer work across sub-agents only after the protocol/image model is approved.

<model>GPT-5 Codex</model>
