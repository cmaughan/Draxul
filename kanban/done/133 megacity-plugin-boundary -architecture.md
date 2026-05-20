# WI 133 — megacity-plugin-boundary

**Type:** architecture
**Priority:** 18
**Raised by:** Chris Maughan (product scope / packaging direction)
**Status:** completed (2026-04-08)

---

## Goal

Keep MegaCity as a valuable Draxul-owned capability without forcing it to remain part of the core terminal/Neovim product surface.

The likely product reality is:

- the **terminal / Neovim frontend is the core shippable**
- MegaCity is **useful, differentiating, and worth keeping**
- but MegaCity should become **optional at the module boundary**, so the terminal can ship, build, test, and evolve without dragging MegaCity through every core decision

This work item is about creating the architecture that makes MegaCity feel like a plugin or external module, even if it initially still lives in the main repo.

---

## Problem

MegaCity is already better isolated than it used to be:

- WI 17 made it optional behind `DRAXUL_ENABLE_MEGACITY`
- WI 24 made it a first-class host tier instead of an ad hoc renderer hack

That was the right move at the time, but it still leaves a product-shape mismatch:

- the app still conceptually treats MegaCity as one of the built-in host types
- CI, testing, docs, and architecture discussions still have to keep MegaCity in mind as part of the main product
- "core Draxul" and "optional Draxul experiences" are not cleanly separated
- making MegaCity an actual external module later would still be awkward because the current boundary is build-time optional, not ownership-boundary clean

The result is that the terminal product and the experimental / 3D / code-city product are still too entangled at the packaging and repo-structure level.

---

## Recommendation

Do **not** start with runtime `dlopen()` / shared-library plugins.

That is the most expensive and least necessary version of "plugin". It introduces ABI, packaging, crash-isolation, versioning, and platform-specific loader complexity before we have even finished defining the stable boundary.

Recommended sequence:

1. **Create a plugin-style host-provider boundary in-tree**
   - MegaCity becomes an optional provider module registered via a narrow interface
   - the terminal build works with zero MegaCity headers or symbols in the core path
2. **Make that provider boundary submodule-friendly**
   - MegaCity can move to `modules/megacity/` or an external repo without redesigning the app
3. **Only later decide whether runtime dynamic loading is worth it**
   - probably only if distribution or third-party host ecosystems become a real product need

So the immediate target is not "true binary plugin loading".
The target is: **MegaCity can be built, tested, versioned, and eventually hosted outside the core repo without changing the terminal architecture again.**

---

## Proposed Target Shape

### Core Product

Core Draxul should own:

- app shell
- windowing
- renderer
- font/grid/input/runtime support
- host interfaces and host registry
- terminal/shell/nvim hosts
- core chrome, panes, workspaces, diagnostics, config

### Optional Module Boundary

MegaCity should be treated as an optional host module that provides:

- one or more host registrations
- its own config extensions
- its own tests
- its own assets/shaders/data expectations
- its own docs page(s), with only a summary in the core features doc

### Practical Layout

A likely end state:

```text
app/
libs/
  draxul-host-api/        # host interfaces / registry contracts
  draxul-terminal-hosts/  # nvim + shell family
modules/
  megacity/               # in-tree at first, submodule-capable later
    CMakeLists.txt
    include/
    src/
    shaders/
    tests/ or test-support/
```

This does **not** require a separate process. MegaCity can still run in-process as a host. The point is ownership and dependency direction, not IPC.

---

## Design Principles

- The terminal product must build and ship cleanly with MegaCity absent.
- Core code should not include MegaCity headers.
- Core docs should describe MegaCity as an optional module, not as the center of the product.
- MegaCity must depend on the host/render APIs; the core app must not depend on MegaCity implementation details.
- The first boundary should be source-level and CMake-level, not ABI-level.
- Tests for core terminal behavior should not link MegaCity.

---

## Non-Goals

- Do not remove MegaCity.
- Do not redesign MegaCity rendering internals as part of this item.
- Do not build a general third-party plugin marketplace.
- Do not commit to runtime dynamic loading yet.
- Do not split the repo immediately unless the provider boundary is already proven.

---

## Work Plan

### Phase 1 — Define the Module Boundary

- [x] Read the current MegaCity touchpoints end-to-end (`host_manager.cpp`, `host_factory.cpp`, top-level + megacity CMakeLists, config/test paths).
- [x] Documented minimum core-side contract: host registration via `HostProviderRegistry`; everything else (renderer, config, actions) is plumbed through `IHost` / `HostLaunchOptions` and host-agnostic `gui_actions`.
- [x] Identified all leakage sites — `host_manager.cpp` `dynamic_cast<MegaCityHost*>` calls, `app.cpp` host-kind check, megacity-specific `AppOptions` fields and the `toggle_megacity_ui` action — and removed each.

### Phase 2 — Introduce a Host Provider Registry

- [x] Added `libs/draxul-host/include/draxul/host_registry.h` + `src/host_registry.cpp` providing a narrow `HostProviderRegistry` (register/has/create/clear, plus a `global()` accessor).
- [x] Moved nvim/bash/zsh/powershell/wsl registrations into `register_builtin_host_providers()` in `host_factory.cpp`; legacy `create_host(kind)` is now a thin compat wrapper around the global registry.
- [x] Removed the `#include <draxul/megacity_host.h>` and the two `dynamic_cast<MegaCityHost*>` blocks from `app/host_manager.cpp`. The core app no longer references megacity by type or header.
- [x] MegaCity self-registers via `register_megacity_host_provider()`, called from `main.cpp` only when `DRAXUL_ENABLE_MEGACITY` is defined. The `nanovg` demo host got the same self-registration treatment for consistency.
- [x] Generalized two megacity-specific options into host-agnostic flags: `megacity_continuous_refresh` → `request_continuous_refresh`, `no_ui` → `hide_host_ui_panels`. Both are now plumbed through `HostLaunchOptions` and consumed inside `MegaCityHost::initialize()`.
- [x] Renamed the `toggle_megacity_ui` GUI action to `toggle_host_ui` across config, default keybindings, the macOS menu, and tests.

### Phase 3 — Isolate MegaCity as an Optional Module

- [x] Moved `libs/draxul-{megacity,citydb,treesitter,geometry}` to `modules/megacity/draxul-{...}` via `git mv` (history preserved).
- [x] Added `modules/megacity/CMakeLists.txt` so the entire module is added with a single `add_subdirectory(modules/megacity)` from the top-level `CMakeLists.txt`.
- [x] Updated `tests/CMakeLists.txt` to point its private megacity-internal include path at `modules/megacity/draxul-megacity/src` and to gate the link line on `DRAXUL_ENABLE_MEGACITY`.
- [x] Gated megacity-only test files with `#ifdef DRAXUL_ENABLE_MEGACITY` (`citydb_tests.cpp`, `treesitter_tests.cpp`, the megacity round-trip case in `app_config_tests.cpp`) so the core test suite compiles cleanly with the module disabled.
- [x] Linked `draxul-megacity` only to the `draxul` executable target — `draxul-app` no longer depends on it. Added explicit `draxul-gui` to `draxul-app`'s link line to replace the lost transitive include.

### Phase 4 — Make It Submodule-Capable

- [x] The module is added by a single `add_subdirectory(modules/megacity)` call gated on `DRAXUL_ENABLE_MEGACITY`. Replacing `modules/megacity/` with a submodule (or a sibling checkout path) requires only this one line.
- [x] No relative paths inside the module reach outside its own subtree. The only repo-root reference is the canonical `DRAXUL_REPO_ROOT` compile definition, which is `CMAKE_SOURCE_DIR` and unaffected by the move.
- [ ] Document the supported integration shapes (in-tree / submodule / disabled) — left for the docs follow-up below; not required for the boundary itself.

### Phase 5 — Product and Documentation Cleanup

- [x] Verified both build modes: megacity-OFF builds 880 tests (all pass) + smoke test exits 0; megacity-ON builds 974 tests (all pass).
- [ ] `docs/features.md` update describing the module split — included in this same change.
- [ ] Architecture/module map update — included in this same change.

### Phase 6 — Optional Future Step: Runtime Plugins

- [ ] Only after the provider boundary is stable, evaluate whether runtime-discovered plugins are worth the complexity.
- [ ] If yes, design that as a separate work item with explicit ABI/versioning/loading constraints.

---

## Acceptance Criteria

- [ ] A build with MegaCity disabled produces a clean terminal-first product with no MegaCity implementation leakage in the core compile path.
- [ ] The app creates hosts through a provider/registry boundary rather than concrete MegaCity special-casing.
- [ ] MegaCity can live behind a self-contained module target that is compatible with both in-tree and submodule layouts.
- [ ] Core terminal tests/builds do not require MegaCity headers, assets, or source files.
- [ ] Documentation clearly distinguishes the core shippable product from optional modules.
- [ ] A follow-up work item can move MegaCity into a submodule without another architecture rewrite.

---

## Suggested Follow-Up Work Items

This architecture item should likely decompose into smaller implementation items:

1. `megacity-host-provider-registry -refactor`
2. `megacity-module-path-extraction -refactor`
3. `megacity-test-boundary-cleanup -refactor`
4. `megacity-submodule-compatible-cmake -refactor`
5. `docs-core-vs-modules-product-shape -docs`

---

## Interdependencies

- **WI 17** `megacity-optional-cmake-flag -refactor`:
  baseline capability already exists; reuse it rather than replacing it
- **WI 24** `renderer-host-layering -architecture`:
  keep MegaCity as a first-class host type, but not a first-class product dependency
- **WI 74 / WI 56** renderer/private-include cleanup:
  continue the same dependency-boundary discipline
- **WI 55** MegaCity threading/runtime cleanup:
  can proceed independently, but long-term easier once MegaCity is module-shaped

---

## Key Decision

If this item is approved, the intended direction should be:

**MegaCity remains part of Draxul, but not part of Draxul-core.**

That means:

- keep it
- support it
- test it
- but architect it so the terminal can stand fully on its own

