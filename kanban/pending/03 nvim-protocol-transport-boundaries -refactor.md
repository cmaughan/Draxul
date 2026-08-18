# Split Neovim protocol and process transport

**Type:** refactor
**Priority:** P1 / sequence 03
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 4

## Goal

Separate platform-neutral MPack/RPC value, redraw, and input logic from threaded
RPC and child-process transport while retaining compatibility during migration.

## Boundary verification

- [ ] Classify `nvim_process.cpp`, `mpack_codec.cpp`, `rpc.cpp`, `ui_events.cpp`,
  `input.cpp`, and all public headers by required dependencies and callers.
- [ ] Verify `MpackValue`, RPC records, `IRpcChannel`, `UiEventHandler`, and
  `NvimInput` can move without namespace or behavior changes.
- [ ] Record protocol's real public grid/type requirements from `nvim_ui.h`.
- [ ] Confirm `<mpack.h>` and SDL are implementation-only dependencies.
- [ ] Map codec/UI/input tests separately from RPC/process/fragmentation/backpressure tests.

## Implementation and migration

- [ ] Add `draxul-nvim-protocol` for values, channel contract, codec, UI decoder, and input encoder.
- [ ] Make MPack, SDL, and performance PRIVATE to protocol implementation as applicable.
- [ ] Add `draxul-nvim-transport` for `NvimProcess`, `NvimRpc`, reader thread, and request tracking.
- [ ] Keep `draxul-nvim` as a compatibility aggregate/forwarding include surface.
- [ ] Migrate fake channels and `UiRequestWorker` to protocol-only linkage.
- [ ] Migrate RPC fake and `NvimHost` to transport plus protocol.
- [ ] Remove compatibility only after direct consumers and include paths are settled.

## Unit tests

- [ ] Link codec, malformed/truncated MPack, redraw, and input tests to protocol only.
- [ ] Link fragmentation, backpressure, request/response, crash, partial-init, and process tests to transport.
- [ ] Add public-header/link-isolation consumers for both targets.
- [ ] Preserve fake `IRpcChannel` as the standard UI/input seam.
- [ ] Build the owning core test aggregate and `draxul-rpc-fake`; run the focused CTest selection.

## Cross-platform validation

- [ ] Windows: validate pipe reads/writes, process startup failure, and shutdown.
- [ ] macOS: validate POSIX pipe/fork-exec startup failure and shutdown.
- [ ] Preserve MPack extension handling and partial-message accumulation on both platforms.
- [ ] Verify reader-thread restrictions and callbacks remain unchanged.
- [ ] Confirm no Vulkan/Metal dependency or behavior enters either boundary.

## Agent documentation and tooling

- [ ] Update `docs/module-map.md` and canonical Nvim orientation guidance.
- [ ] Add focused protocol/transport build targets to label-level validation where useful.

## Acceptance criteria

- [ ] Protocol tests build without child-process or threaded RPC implementation.
- [ ] Transport owns all OS process/pipe and reader-thread behavior.
- [ ] MPack is no longer a PUBLIC dependency solely because of implementation use.
- [ ] Existing Nvim host behavior, inputs, redraw decoding, and shutdown are preserved.
- [ ] Full tests, smoke, and available cross-platform validation pass.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One Nvim
owner freezes value/channel headers. Protocol tests and process transport may then
be independent, but final `NvimHost` linking coordinates with
`kanban/pending/02 host-layer-static-libraries -refactor.md`.
