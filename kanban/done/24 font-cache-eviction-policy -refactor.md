# 24 Font Cache Eviction Policy

## Why This Exists

`TextService::Impl::font_choice_cache` is a `std::string → int` map that caches which font index
to use for each text cluster. It grows without bound over a session. A terminal session rendering
mixed CJK, emoji, symbols, and ASCII can easily accumulate tens of thousands of distinct cluster
strings over hours of use, slowly consuming memory.

The `GlyphCache` handles texture exhaustion via atlas reset, but the CPU-side font-choice cache
has no equivalent eviction mechanism.

**Source:** `libs/draxul-font/src/text_service.cpp` — `font_choice_cache`.
**Raised by:** Claude (primary), GPT, Gemini (all three flag this).

## Goal

Add a size-bounded eviction policy to `font_choice_cache`.

## Implementation Plan

- [x] Read `libs/draxul-font/src/text_service.cpp` to understand how `font_choice_cache` is populated and accessed.
- [x] Decide on implementation:
  - **Option A (simple):** Wrap the map in a helper that discards the entire cache when it exceeds `kMaxFontCacheEntries`. This is O(1) overhead and sufficient for most sessions (the hot entries will be re-populated quickly).
  - **Option B (LRU):** Use a doubly-linked list + unordered_map for true LRU eviction. More code but better hit-rate for genuinely large sessions.
  - Start with Option A unless Gemini's long-run test (see notes) reveals meaningful performance impact.
- [x] Implement the chosen policy.
- [x] Add a constant `kMaxFontChoiceCacheEntries` (e.g., `4096`) to the implementation.
- [x] Add a test that fills the cache beyond the limit and verifies it is bounded (see notes on Gemini's suggestion for a long-run cache test).
- [x] Run `clang-format`.
- [x] Run `ctest --test-dir build`.

## Completion Notes

- Implemented the simple clear-on-cap policy in `TextService` via a small cache helper instead of a full LRU.
- Added `TextServiceConfig::font_choice_cache_limit` with a default of `4096`.
- Added `TextService::font_choice_cache_size()` as a narrow public seam so the bounded-cache behavior can be verified through the public API.
- Added a regression test that drives many unique clusters through `resolve_cluster()` and verifies the cache stays within the configured limit.

## Notes

Gemini suggested a "long-run text-service test for cache growth and repeated atlas resets" as a
stability test. A simple unit test that inserts N > kMax entries and asserts cache size ≤ kMax
covers the eviction correctness without requiring a long-running session.

## Sub-Agent Split

Single agent. Confined to `libs/draxul-font/src/text_service.cpp`.
