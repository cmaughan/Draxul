# Split Neovim protocol and process transport

**Type:** refactor
**Priority:** P1 / sequence 03
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 4

## Goal

Separate platform-neutral MPack/RPC value, redraw, and input logic from threaded
RPC and child-process transport while retaining compatibility during migration.

## Boundary verification

- [x] Classify `nvim_process.cpp`, `mpack_codec.cpp`, `rpc.cpp`, `ui_events.cpp`,
  `input.cpp`, and all public headers by required dependencies and callers.
- [x] Verify `MpackValue`, RPC records, `IRpcChannel`, `UiEventHandler`, and
  `NvimInput` can move without namespace or behavior changes.
- [x] Record protocol's real public grid/type requirements from `nvim_ui.h`.
- [x] Confirm `<mpack.h>` and SDL are implementation-only dependencies.
- [x] Map codec/UI/input tests separately from RPC/process/fragmentation/backpressure tests.

## Implementation and migration

- [x] Add `draxul-nvim-protocol` for values, channel contract, codec, UI decoder, and input encoder.
- [x] Make MPack, SDL, and performance PRIVATE to protocol implementation as applicable.
- [x] Add `draxul-nvim-transport` for `NvimProcess`, `NvimRpc`, reader thread, and request tracking.
- [x] Keep `draxul-nvim` as a compatibility aggregate/forwarding include surface during migration.
- [x] Migrate fake channels and `UiRequestWorker` to protocol-only linkage.
- [x] Migrate RPC fake and `NvimHost` to transport plus protocol.
- [x] Remove compatibility after migrating the remaining test consumers to the narrow transport header.

## Unit tests

- [x] Link codec, malformed/truncated MPack, redraw, and input tests to protocol only.
- [x] Link fragmentation, backpressure, request/response, crash, partial-init, and process tests to transport.
- [x] Add public-header/link-isolation consumers for both targets.
- [x] Preserve fake `IRpcChannel` as the standard UI/input seam.
- [x] Build the owning core test aggregate and `draxul-rpc-fake`; run the focused CTest selection.

## Cross-platform validation

- [x] Windows: validate pipe reads/writes, process startup failure, and shutdown.
- [x] macOS: validate POSIX pipe/fork-exec startup failure and shutdown.
- [x] Preserve MPack extension handling and partial-message accumulation on both platforms.
- [x] Verify reader-thread restrictions and callbacks remain unchanged.
- [x] Confirm no Vulkan/Metal dependency or behavior enters either boundary.

## Agent documentation and tooling

- [x] Update `docs/module-map.md` and canonical Nvim orientation guidance.
- [x] Add focused protocol/transport build targets to label-level validation where useful.

## Acceptance criteria

- [x] Protocol tests build without child-process or threaded RPC implementation.
- [x] Transport owns all OS process/pipe and reader-thread behavior.
- [x] MPack is no longer a PUBLIC dependency solely because of implementation use.
- [x] Existing Nvim host behavior, inputs, redraw decoding, and shutdown are preserved.
- [x] Full tests, smoke, and available cross-platform validation pass.

## Validation

- 2026-09-21 macOS Debug: focused Nvim protocol/transport targets, both
  public-header link-isolation consumers, and `draxul-rpc-fake` built; the Nvim
  CTest label passed 5/5.
- 2026-09-21 macOS Debug: the core aggregate passed 18/18 after stopping an
  existing repo-built Draxul server that held the process singleton; the
  same-cache Metal smoke passed.
- Windows process/pipe validation and the cross-platform MPack accumulation
  check remain open.
- 2026-09-22 source audit: the remaining eight test consumers now include
  `<draxul/nvim_transport.h>` directly; the unused `draxul-nvim` INTERFACE target
  and `<draxul/nvim.h>` / `<draxul/nvim_rpc.h>` forwarding headers were removed.
  A Debug configure and app build passed, followed by focused protocol, transport,
  RPC-fake, test, and public-header isolation target builds. The four focused CTest
  shards passed in 3.52 seconds. `scripts/gen_deps.py` regenerated the canonical
  DOT/SVG dependency graph; it contains the protocol and transport leaves and no
  exact `draxul-nvim` aggregate node.
- The final normal Debug validation passed all 45 unit CTest entries, startup
  smoke, and all five default Metal render comparisons.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md`. One Nvim
owner freezes value/channel headers. Protocol tests and process transport may then
be independent, but final `NvimHost` linking coordinates with
`kanban/done/02 host-layer-static-libraries -refactor.md`.

## Windows closeout (2026-09-22)

The final MSVC products aggregate passed 48/48, including protocol, fragmented
MPack, pipe/process-failure, and shutdown coverage; same-cache smoke passed.
