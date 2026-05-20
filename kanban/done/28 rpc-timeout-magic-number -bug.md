# 28 rpc-timeout-magic-number -bug

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.claude.md [C]*

## Problem

`libs/draxul-nvim/src/rpc.cpp` hard-codes the RPC request timeout as the number `5` (seconds)
with no named constant and no documentation of why five seconds was chosen.  On slow machines
or when nvim is executing a large indexing or plugin-init operation, this produces spurious
timeout errors that are logged as failures and are indistinguishable from genuine hangs.

Secondary issue: the timeout is not user-configurable.  A power user with a large workspace
cannot adjust it without rebuilding from source.

## Acceptance Criteria

- [x] The magic `5` is replaced with a named constant: `static constexpr auto kRpcRequestTimeout = std::chrono::seconds{5};`
      with a comment explaining the rationale (e.g., "conservative default; long enough for
      most nvim operations, short enough to surface genuine hangs").
- [x] The constant is placed in a header or at the top of the TU, not buried in the call site.
- [x] (Optional, lower priority) Deferred — config wiring depends on config-layer-decoupling refactor.
      The named constant is in place and trivially adjustable.
- [x] Existing RPC tests pass.

## Implementation Plan

1. Read `libs/draxul-nvim/src/rpc.cpp` — find all timeout references.
2. Extract to named constant at top of file (or in a companion header if it needs to be
   configurable).
3. Add a one-line comment: *"5 s is the conservative default; increase via rpc_timeout_seconds
   in [advanced] if nvim indexing causes spurious timeouts on large workspaces."*
4. If config wiring is in scope: add `rpc_timeout_seconds` to `ConfigDocument` defaults,
   validation (must be > 0, ≤ 60), and pass through `AppConfig` → `NvimRpcOptions`.
5. Run `cmake --build build --target draxul-tests && ctest`.

## Files Likely Touched

- `libs/draxul-nvim/src/rpc.cpp`
- (optional) `libs/draxul-nvim/include/draxul/nvim_rpc_options.h` or equivalent
- (optional) config document / AppConfig for the tunable path

## Interdependencies

- Independent of other open work items.
- If the config-tunable path is implemented, coordinate with `14 config-layer-decoupling -refactor`.
