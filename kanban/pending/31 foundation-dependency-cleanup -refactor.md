# Reduce foundation-layer dependency churn

**Type:** refactor
**Priority:** 31
**Raised by:** Claude, GPT/Codex

## Goal

`draxul-types` contains unrelated logging, perf, BMP/filesystem helpers, Unicode data, results, and product-aware `HostKind`. Reduce bottom-layer churn without creating cycles or a new generic dumping ground.

## Implementation plan

- [ ] Generate the CMake target graph and inventory every public header/source in `draxul-types` by actual consumers.
- [ ] Keep truly universal value/error/event types at the bottom; propose narrowly named targets for image/BMP and performance facilities only where the graph improves.
- [ ] Move host identity/parsing toward `draxul-host` or a neutral provider descriptor so adding an optional product does not edit the lowest layer.
- [ ] Use forwarding headers/type aliases temporarily to keep migrations incremental; delete them once all consumers move.
- [ ] Tighten `PUBLIC` to `PRIVATE` link/include propagation based on public-header needs.
- [ ] Audit `draxul-runtime-support` separately and split only responsibilities with a stable consumer boundary.
- [ ] Reopen, rather than duplicate, completed config-SDL boundary work if that acceptance criterion is currently false.

## Tests and acceptance

- [ ] Add dependency-boundary checks preventing product modules from leaking back into foundation targets.
- [ ] Configure optional modules independently and build public-header consumers.
- [ ] No public API duplication between `src/` and `include/`.
- [ ] Incremental commits keep the full build/tests/smoke green.

## Dependencies and parallelism

Item 12 should establish provider metadata first. This is graph-wide work for one architecture owner; mechanical consumer migrations can be delegated after target boundaries land.

<model>GPT-5 Codex</model>
