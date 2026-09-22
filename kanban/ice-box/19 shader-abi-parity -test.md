# Complete remaining shader ABI and binding parity

**Type:** test
**Priority:** 19
**Raised by:** GPT/Codex, Claude
**Model:** GPT-5 Codex

## Goal

Mechanically validate the remaining high-risk CPU/GPU data layouts and resource
bindings. A one-sided C++, GLSL, or MSL edit must fail before it can silently
corrupt rendering.

## Delivered foundation

The initial grid push-constant slice shipped in commit `258df01`:

- `shaders/contracts/grid_push_constants.toml` records the seven-float,
  28-byte `GridPushConstants` layout plus Metal and Vulkan binding indices.
- `libs/draxul-renderer/src/shared/grid_contract.h` provides the backend-private
  C++ mirror, named binding constants, and compile-time size/alignment/offset
  assertions.
- `tests/shader_abi_parity_tests.cpp` contains four `[shader_abi]` cases that
  compare the manifest with its C++, MSL, and GLSL consumers. The GLSL check is
  a deterministic source scan; it is not compiled SPIR-V reflection.
- Deliberate one-sided field-order and binding edits were proven to fail either
  the renderer build or the parity suite, then reverted.

The shared ACES tone-map contract shipped in commit `7318f41`:

- `shaders/contracts/tone_map_aces.toml` records the shared contract.
- The same parity suite checks manifest consistency and agreement between the
  GLSL and MSL ACES includes.

These delivered contracts remain regression coverage and are not part of the
active implementation checklist below.

## Remaining scope

- [ ] Add a checked contract for the grid `GpuCell`/CPU `Cell` transfer layout,
      including size, alignment, field offsets, style flags, and SSBO bindings
      consumed by the grid background and foreground pipelines.
- [ ] Add checked contracts for the core NanoVG CPU/shader layouts and resource
      indices that cross the renderer boundary.
- [ ] Add checked contracts for the core Markdown CPU/shader layouts and
      resource indices that cross the renderer boundary.
- [ ] Reflect compiled Vulkan SPIR-V modules and compare their blocks,
      offsets, descriptor sets, and bindings with the same checked contracts.
      Source-text scanning is insufficient for this gate.
- [ ] Exercise the contract suite and shader compilation with optional modules
      enabled and disabled. Product-owned shader contracts remain in their
      product repositories.

Do not replace the checked-manifest approach with a flag-day generator unless
the smaller contracts prove generation materially simpler. Do not expose Metal
or Vulkan headers through public renderer interfaces.

## Verification

- [ ] Deliberately perturb one field offset and one binding or texture slot for
      each new core contract and prove the relevant compile-time or runtime
      check fails; revert every perturbation.
- [ ] Prove a one-sided SPIR-V binding/layout change fails reflection even when
      the GLSL source scan would still parse.
- [ ] Compile the affected Metal and Vulkan shader targets after adding each
      contract.
- [ ] Run the optional-module on/off matrix and the focused `[shader_abi]`
      suite on the platforms that provide the required shader toolchains.

## Acceptance criteria

- [ ] `GpuCell`/`Cell`, core NanoVG, and core Markdown CPU/GPU contracts have
      mechanical size, offset, flag, and resource-index validation.
- [ ] Compiled Vulkan SPIR-V reflection agrees with the checked contracts; a
      source-only GLSL check is no longer the authoritative Vulkan gate.
- [ ] One-sided C++/GLSL/MSL changes to every covered contract fail a build or
      focused integrity test.
- [ ] Contract files and validation are deterministic and documented without
      widening backend-header visibility.
- [ ] Focused parity tests, affected shader builds, optional-module matrix,
      aggregate tests, and same-cache smoke pass on their supported platforms.

## Dependencies and ownership

`kanban/ice-box/06 metal-shader-dependency-closure -bug.md` owns incremental
Metal build invalidation. Runtime/source manifest checks remain independent of
that card, while compiled-artifact verification must use correctly rebuilt
shader outputs. Core owns the contracts listed here; product shader contracts
belong to their product repositories.
