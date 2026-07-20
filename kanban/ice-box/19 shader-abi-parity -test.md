# Mechanical shader ABI and binding parity

**Type:** test
**Priority:** 19
**Raised by:** GPT/Codex, Claude

## Gap

C++ structs/constants, GLSL declarations, and Metal declarations manually mirror sizes, offsets, bindings, texture slots, and feature flags. Existing static assertions cover only a subset.

## Implementation plan

- [ ] Inventory shared contracts for grid, NanoVG, Markdown, MegaCity, and SatView; prioritize those written by CPU and consumed directly by shaders.
- [x] Define a small declarative contract source for field type/order/alignment and binding/slot constants, or a checked manifest where generation is not practical. *(checked-manifest chosen — `shaders/contracts/grid_push_constants.toml` — for the grid push-constant block.)*
- [ ] Generate/include C++ static assertions and GLSL/MSL constants from that source without exposing backend headers publicly. *(Partial: C++ static_asserts + backend-private header done; no code generation — the checked-manifest sidesteps generation, see Status.)*
- [ ] Add SPIR-V reflection checks for Vulkan outputs and a Metal-side compile/manifest check for matching resource indices and struct sizes. *(Metal-side resource-index + struct-size manifest check done; Vulkan SPIR-V reflection is CI-only/unimplemented here.)*
- [x] Fail on stale generated files in a repository-integrity test. *(Adapted to checked-manifest: the integrity test fails when any one consumer — C++/MSL/GLSL — diverges from the manifest.)*
- [ ] Migrate one small contract first, then grid, then product modules; avoid a flag-day shader rewrite. *(First small contract (grid push-constants) migrated; no flag-day rewrite. Remaining contracts listed in Status.)*

## Verification

- [ ] Deliberately change a field offset, binding, and texture slot in each backend and prove the check fails.
- [ ] Run the contract check in optional-module on/off configurations.
- [ ] Compile all Vulkan and Metal shader targets after generation.

## Acceptance criteria

- [ ] High-risk CPU/GPU contracts have mechanical size/offset/index validation.
- [ ] A one-sided GLSL/MSL/C++ edit cannot pass the integrity test.
- [ ] Generated artifacts are deterministic and documented.

## Dependencies and parallelism

Item 06 fixes build invalidation first. This safety net should precede large renderer/module refactors and can be owned by a GPU-contract sub-agent.

## Status — 2026-07-19 (macOS/Metal worktree)

**Stays in `pending/`.** The first contract is covered end-to-end on the buildable
(C++ + Metal) half; the authoritative Vulkan half is CI-only and item 06 is still iced.

**Contract covered:** `GridPushConstants` — the per-draw grid vertex push-constant
block (7 tightly-packed `float`s, 28 bytes) written CPU-side once per pane and read by
`grid_bg`/`grid_fg`. Chosen first because it was the smallest CPU→GPU contract and was
previously an *unnamed* anonymous struct (Metal) and a raw `float[7]` (Vulkan) with no
type, name, or assertion — a silent-corruption risk.

**Design (checked-manifest, not generate-and-recompile):**
- Single source of truth: `shaders/contracts/grid_push_constants.toml` (fields +
  offsets/sizes + Metal argument-table indices + Vulkan set/binding/offset).
- C++ writer mirror: `libs/draxul-renderer/src/shared/grid_contract.h`
  (backend-private, no Metal/Vulkan headers) with `static_assert`s on
  `sizeof`/`alignof`/`offsetof` and named binding constants. `metal_renderer.mm` now
  uploads `grid_contract::GridPushConstants` and binds via those named constants.
- Parity test: `tests/shader_abi_parity_tests.cpp` (`[shader_abi]`, 4 cases) re-derives
  the layout and binding/slot indices from EACH consumer and asserts equality with the
  manifest: manifest self-consistency; C++ `sizeof`/`offsetof`/binding constants; MSL
  `struct PushConstants` fields + `[[buffer/texture/sampler(N)]]` indices + a
  text-derived MSL struct **size**; GLSL push-constant block + SSBO/sampler set/binding.

**Proven (deliberate one-sided edits, each reverted):**
- MSL texture slot `[[texture(0)]]`→`(5)` in `grid.metal` → `[shader_abi]` fails `5 == 0` (runtime).
- C++ binding constant `kMetalAtlasTextureIndex 0`→`5` → builds, runtime cross-check fails `5 == 0`.
- C++ field reorder (`screen_w`/`screen_h`) → **compile-time** `static_assert` failure in the renderer build.
- GLSL sampler `binding = 1`→`3` in `grid_fg.frag` → `[shader_abi]` fails `3 == 1` (text scan).

**CI-only / not done here (honest):**
- Authoritative Vulkan check = SPIR-V reflection of compiled modules. Not implemented —
  no `glslc`/SPIR-V tooling on macOS. The GLSL check here is a deterministic TEXT scan
  only; `[bindings.vulkan]` in the manifest is structured so a reflection check can be
  added later against the same source.
- Vulkan shader compilation and the module-on/off matrix run were not exercised here.

**Item 06 dependency (`kanban/ice-box/06 metal-shader-dependency-closure`):** the card
gates this on 06 fixing build invalidation. The checked-manifest approach **sidesteps**
that: the manifest and shader sources are read by the test at **runtime** (no generated
files, no shader recompile in the loop), so a stale-artifact / incremental-rebuild bug
cannot make the parity check pass-when-it-should-fail. No dependency on 06 was introduced.

**Next contracts to migrate (in order):** the 112-byte `GpuCell`/`Cell` grid SSBO
(largest grid contract, same bg/fg pipeline), then the product modules — NanoVG, then
Markdown / MegaCity / SatView push-constant + SSBO contracts.

**Relation to `kanban/pending/27` (renderer/module refactor):** this gives 27 a partial
safety net — any refactor that reorders/resizes the grid push-constant block or moves its
Metal/Vulkan bindings now fails the build (C++ layout) or `[shader_abi]` (bindings/MSL/GLSL)
instead of silently corrupting positioning/scroll/viewport. Coverage is limited to the
grid push-constant contract until the contracts above are migrated.

<model>GPT-5 Codex</model>
