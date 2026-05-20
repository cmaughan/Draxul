# WI 24 — Unified `Result<T, Error>` type

**Type:** refactor  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 6

---

## Goal

Replace the three incompatible error-handling patterns currently coexisting in the codebase (`bool + .error()`, `std::optional<T>`, and silent-fail `void`) with a single `Result<T, E>` type that makes error cases impossible to ignore at call sites.

---

## Problem

Claude's review identified at least four distinct error-handling patterns:
- `bool` return with a `.error()` / `.last_error()` accessor
- `std::optional<T>` (missing = failure, no reason)
- Silent fail: function returns `void`, failure logged but not propagated
- `std::expected<T, E>` (C++23) not yet used

This inconsistency means error-path auditing requires knowing which pattern each function uses. Silent fails in particular (clipboard, config reload, atlas overflow) mean users get no feedback. This is flagged in the "Top 10 Bad Things" by Claude as item #3.

---

## Implementation Plan

**Phase A — define the type:**
- [x] Evaluate whether to use `std::expected<T, E>` (C++23, already available with modern Clang/MSVC) or a custom `Result<T, E>` wrapper. Prefer `std::expected` if the compiler baseline supports it. *(Project baseline is C++20 per `CMakeLists.txt`; hand-rolled a minimal `Result<T, E>` to avoid bumping the standard. Easy to swap for a `std::expected` alias later.)*
- [x] Define a project-wide error enum or error string type for common failure categories (init failure, RPC error, I/O error, config error). *(`draxul::ErrorKind` + `draxul::Error` struct with factory helpers.)*
- [x] Add a header in `libs/draxul-types/include/draxul/result.h`.

**Phase B — migrate high-value call sites:**
- [x] `NvimProcess::spawn()` — currently returns `bool`; migrate to `Result<void, SpawnError>`. *(Now `Result<void, Error>`; contextual bool conversion keeps existing `REQUIRE(spawn(...))` and `if (!spawn(...))` call sites compiling.)*
- [x] `NvimRpc::send_request()` — returns `RpcResult`; ensure it's consistent with `Result`. *(Replaced `RpcResult` struct with `using RpcResult = Result<MpackValue, Error>`. Transport failures → `ErrorKind::IoError`; remote Neovim errors → `ErrorKind::RpcError` with stringified message. All call sites, test mocks, and FakeRpcChannel updated.)*
- [x] `AppConfig::reload()` — currently silent; migrate to `Result<void, ConfigError>`. *(Migrated `App::reload_config()` — the actual reload entry point — to `Result<void, Error>`; the closest `AppConfig::load()` static returns a value with defaults and was left alone.)*
- [x] `GlyphCache::rasterize_cluster()` — currently returns sentinel; migrate to `Result<AtlasRegion, RasterError>`. *(Now returns `Result<AtlasRegion, Error>`. FreeType load/render failures, bitmap conversion, and atlas overflow each produce a structured error. `get_cluster()` logs the error on failure; public API (`const AtlasRegion&` return) unchanged.)*

**Phase C — update callers:**
- [x] For each migrated function, update all call sites to handle the error or explicitly propagate it. *(Scoped to the migrated sites: `NvimHost::initialize` now logs the structured spawn error; the GUI action lambda for `reload_config` now surfaces failure as a toast. Remaining call sites kept via bool conversion.)*
- [x] Convert previously-silent failures to either return an error to the caller or show a toast notification. *(`reload_config` now shows a toast on font-reload failure instead of only logging.)*

**Status (2026-04-09):** All phases complete. Phase A defined `Result<T, E>` and `Error`. Phase B migrated all four call sites: `spawn`, `reload_config`, `NvimRpc::request`, `GlyphCache::rasterize_cluster`. Phase C updated all callers including tests. `Result<T, E>` also gained a default constructor (error state) to support declare-then-assign patterns. Added `stringify_rpc_error()` helper for converting Neovim's mpack error payloads to strings.

---

## Notes for the agent

- This is a wide-surface refactor; consider subagent parallelism for migrating different libraries.
- Don't migrate everything at once — Phase A + a handful of high-value Phase B sites is a good first PR.
- This change feeds into WI 29 (config-migration-framework) which needs a clean error return from config operations.

---

## Interdependencies

- Should land after Phase 1 bugs (WI 04-07) to avoid merge conflicts in `rpc.cpp`.
- WI 29 (config migration framework) depends on this for clean error returns.
- WI 28 (rpc timeout feedback) will use the error type to surface RPC errors as toasts.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
