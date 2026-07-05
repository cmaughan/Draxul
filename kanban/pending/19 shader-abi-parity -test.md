# Mechanical shader ABI and binding parity

**Type:** test
**Priority:** 19
**Raised by:** GPT/Codex, Claude

## Gap

C++ structs/constants, GLSL declarations, and Metal declarations manually mirror sizes, offsets, bindings, texture slots, and feature flags. Existing static assertions cover only a subset.

## Implementation plan

- [ ] Inventory shared contracts for grid, NanoVG, Markdown, MegaCity, and SatView; prioritize those written by CPU and consumed directly by shaders.
- [ ] Define a small declarative contract source for field type/order/alignment and binding/slot constants, or a checked manifest where generation is not practical.
- [ ] Generate/include C++ static assertions and GLSL/MSL constants from that source without exposing backend headers publicly.
- [ ] Add SPIR-V reflection checks for Vulkan outputs and a Metal-side compile/manifest check for matching resource indices and struct sizes.
- [ ] Fail on stale generated files in a repository-integrity test.
- [ ] Migrate one small contract first, then grid, then product modules; avoid a flag-day shader rewrite.

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

<model>GPT-5 Codex</model>
