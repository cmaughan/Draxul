# Reduce foundation-layer dependency churn

**Type:** refactor
**Priority:** 31
**Raised by:** Claude, GPT/Codex

## Goal

`draxul-types` contains unrelated logging, perf, BMP/filesystem helpers, Unicode data, results, and product-aware `HostKind`. Reduce bottom-layer churn without creating cycles or a new generic dumping ground.

## Implementation plan

- [x] Generate the CMake target graph and inventory every public header/source in `draxul-types` by actual consumers.
- [x] Keep truly universal value/error/event types at the bottom; extract narrowly named BMP and performance targets where the graph improves.
- [x] Move host identity/parsing to the neutral `draxul-host-identity` target so it is no longer owned by the value-type archive.
- [x] Migrate consumers directly; no forwarding or duplicate public headers remain.
- [x] Tighten `PUBLIC` to `PRIVATE` link/include propagation based on public-header needs.
- [x] Audit `draxul-runtime-support` separately and preserve its stable public consumer boundary while its session implementation is split by platform.
- [x] Declare the current `draxul-host -> draxul-nvim` dependency directly instead of receiving Nvim headers transitively through `draxul-runtime-support`.
- [x] Inventory SDL keycode use outside `draxul-window`; keep config's SDL implementation dependency private and make `draxul-nvim`'s SDL dependency private.
- [x] Verify the completed config-SDL acceptance remains true rather than reopening or duplicating the old card.

## Tests and acceptance

- [x] Add configure-time dependency-boundary checks preventing product modules from leaking back into foundation targets.
- [x] Configure all optional module gates and build independent public-header consumers.
- [x] Add a link-isolation build that consumes only `draxul-host` and proves its declared dependency closure links.
- [x] No public API duplication between `src/` and `include/`.
- [x] Incremental boundaries remain build-green. Per the implementation-session constraint, validation was build-only; no test/helper/app executable was run.

## Completed inventory

The remaining `draxul-types` public surface is the universal event, input, grid,
highlight, text/atlas, result, logging, path, Unicode, and small value-type
contracts. Its compiled implementation is now limited to logging and runtime-path
resolution. `perf_timing.*`, `bmp.*`, and `host_kind.h` moved without forwarding
copies to `draxul-performance`, `draxul-bmp`, and `draxul-host-identity`.

SDL keycodes remain implementation details in config parsing and Neovim input
encoding; neither target exposes SDL types in a public header. Config already linked
SDL privately, and Nvim now does too, so the deleted historical config-SDL card's
acceptance remains satisfied.

Build-only validation:

- `cmake --preset default`
- `cmake --build build --config Release --target draxul-performance draxul-types draxul-bmp draxul-performance-link-isolation draxul-bmp-link-isolation`
- `cmake --build build --config Release --target draxul-host-link-isolation`

## Dependencies and parallelism

Item 12 should establish provider metadata first. This is graph-wide work for one architecture owner; mechanical consumer migrations can be delegated after target boundaries land.

<model>GPT-5 Codex</model>
