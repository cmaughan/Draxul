# Split SatView into stable library boundaries

**Type:** refactor
**Priority:** 26
**Raised by:** GPT/Codex, Claude

## Goal

SatView's single target currently mixes catalogs, network/cache services, propagation, scene records, UI/host state, and both render backends. Split it without altering user behavior or cross-platform scene parity.

## Target shape

- `draxul-satview-core`: catalogs, coordinates, filters, ephemerides, propagation.
- `draxul-satview-services`: HTTP/cache refresh above item 00's transport.
- `draxul-satview-scene`: backend-neutral vertices, uniforms, draw records, snapshots.
- `draxul-satview-renderer`: private Vulkan/Metal implementations consuming one scene contract.
- `draxul-satview-host`: ImGui, input, camera, config, orchestration.

## Implementation plan

- [x] Wait for current observatory/boundary/text-atlas work and item 18.
- [x] Generate the current include/target dependency graph and classify every source file. (Full file->target table produced; every source classified by its `#include` edges before any move.)
- [x] Create targets in dependency order, starting with core and scene; use `PRIVATE` links by default. (core -> scene/services -> renderer -> host, one green commit per boundary.)
- [x] Move services after item 00 so shell transport is not preserved in the new boundary. (`draxul-satview-services` links `draxul-http` PUBLIC; it is the only SatView boundary that touches network/cache.)
- [x] Move backend selection behind the renderer target and keep public headers backend-neutral. (`draxul-satview-renderer` compiles exactly one backend per platform and exposes no public headers; no Metal/Vulkan type appears in any public header.)
- [x] Reduce duplicated Apple/non-Apple source lists with shared source sets plus backend additions. (Only the renderer carries the per-backend source + Metal/Vulkan links; every other target is a single platform-independent list.)
- [x] Consolidate SatView/MegaCity-style asset-path resolution and ImGui attachment only through a demonstrated lower-library contract; do not make the product modules depend on one another. (Took the "leave duplicated" option: SatView keeps its own `resolve_satview_asset_path` in core; no satview<->megacity edge was introduced.)
- [x] Keep registration at the executable boundary and preserve `DRAXUL_ENABLE_SATVIEW=OFF` zero-coupling. (`register_satview_host_provider` still called from `app/main.cpp`; SATVIEW=OFF builds link zero satview libraries/objects — verified.)
- [x] Land each boundary as a buildable commit with no behavior changes.

## Tests and acceptance

- [x] Assign focused tests to the narrowest target and keep the host smoke test. (The repo uses a single `draxul-tests` target, as MegaCity does; tests reach each boundary's private `src/` via include dirs — host + renderer — and public headers resolve transitively. `satview_host_smoke_tests` stays the key behavioral gate and is green.)
- [ ] Configure/build with SatView ON and OFF on Windows and macOS paths. (macOS ON+OFF verified green here; **Windows/Vulkan pending CI** — this machine cannot build the Vulkan backend or the Windows path.)
- [ ] Vulkan and Metal consume identical scene records; no catalog/filter logic enters backends. (Both backends consume one scene contract, `SatViewScenePass`; no catalog/filter code lives in the renderer target. Metal verified locally and the `satview_ring_render_state_tests` parity test is green; **Vulkan side pending CI**.)
- [x] Full build, focused SatView tests, `ctest`, render/startup check, and smoke pass. (macOS/Metal: full build green; `[satview]` 165 cases / 28415 assertions green at every boundary; `do.py smoke` exit 0; `ctest` 11/12 — the single failure is a **pre-existing, unrelated** trailing-newline drift in `docs/config-keys.generated.md` vs `render_config_docs_markdown()`, no satview content, both inputs byte-identical to the base commit.)

## Dependencies and parallelism

Depends on 00/18 and active SatView work. After source classification, core/services and scene/renderer moves can be delegated, but one owner must control CMake and public boundaries.

## Status (2026-07-20)

Landed on branch `worktree-agent-a05d0b68dcc497cb0` as five dependency-ordered
green commits, one per boundary (`satview: extract draxul-satview-{core,scene,
services,renderer,host}`). The single `draxul-satview` target is now:

- `draxul-satview-core` — catalogs, coordinates/frames, filters, ephemerides,
  propagation, SGP4 (PUBLIC draxul-types; PRIVATE celestrak-sgp4). Pure compute.
- `draxul-satview-scene` — backend-neutral vertex/uniform/instance records, the
  `SatViewScenePass` snapshot contract, and celestial-geometry builders
  (PUBLIC core, draxul-renderer). No Metal/Vulkan types in public headers.
- `draxul-satview-services` — HTTP/cache catalog + cloud services above the
  draxul-http transport (PUBLIC core, draxul-http).
- `draxul-satview-renderer` — PRIVATE Metal (`satview_render.mm`) / Vulkan
  (`satview_render_vk.cpp`, wired for CI, not compiled on macOS). No public
  headers; supplies `SatViewScenePass::State`/`record()` via the scene pImpl.
- `draxul-satview-host` — ImGui/input/camera/config-IO/simulation orchestration
  plus provider registration; the target the executable links.

`app/main.cpp` is unchanged (still includes `<draxul/satview/satview_host.h>`
and calls `register_satview_host_provider`); only the CMake link line moved to
`draxul-satview-host`. Kept in `kanban/pending/` because two acceptance lines
remain: Windows/Vulkan CI must confirm the OFF/ON configure+build and the
Metal/Vulkan scene-record parity on the non-Apple path.

<model>GPT-5 Codex</model>
