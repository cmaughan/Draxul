# Split Kanban core from grid-host integration

**Type:** refactor
**Priority:** P2 / sequence 04
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 5

## Goal

Keep board, storage, layout, and navigation in `draxul-kanban`, and move
`KanbanHost` plus provider registration into `draxul-kanban-host`.

## Boundary verification

- [x] Confirm only `kanban_host.cpp`/`kanban_host.h` require `GridHostBase` and host registration.
- [x] Inventory public header include needs for board, store, layout, and navigation.
- [x] Verify SDL is needed only privately by navigation implementation.
- [x] Split test inventory into pure core cases and host lifecycle/rendering cases.
- [x] Compare the intended split with the existing Markdown core/host precedent.

## Implementation and migration

- [x] Remove host sources from `draxul-kanban` without changing their directory initially.
- [x] Add `draxul-kanban-host` for `KanbanHost` and provider registration.
- [x] Link core only to required value/filesystem dependencies; keep SDL private.
- [x] Link host to core plus `draxul-grid-host`/host API.
- [x] Update executable provider linkage and test targets.
- [x] Avoid splitting board/store/layout/navigation into additional micro-libraries.

## Unit tests

- [x] Link board, store, layout, and navigation tests to `draxul-kanban` only (plus their direct SDL test-header dependency).
- [x] Keep `kanban_host_tests.cpp` on the host target and existing grid-host fixture.
- [x] Add a Kanban core public-header/link-isolation build.
- [x] Build `draxul-kanban`, `draxul-kanban-host`, and `draxul-test-markdown-kanban`.
- [x] Run the equivalent active-cache gate: `python3 do.py test debug --label kanban`.

## Cross-platform validation

- [x] Exercise filesystem ordering/moves on macOS path semantics.
- [x] Exercise filesystem ordering/moves on Windows path semantics.
- [x] Exercise SDL navigation key constants on macOS.
- [x] Exercise SDL navigation key constants on Windows.
- [x] Confirm host rendering behaves the same through the existing grid contract.
- [x] Confirm no Vulkan/Metal backend-specific API enters core.

## Agent documentation and tooling

- [x] Update `docs/module-map.md` and any target examples naming Kanban.
- [x] Ensure `python do.py test --label kanban` builds the host/core owner target correctly.

## Acceptance criteria

- [x] Pure Kanban consumers no longer receive the full host/terminal/Nvim closure.
- [x] Core and host tests link only their owning target boundaries.
- [x] Host registration and user-visible board behavior are unchanged.
- [x] Focused tests, full build, and smoke pass on available platforms.

Local validation on macOS: the exact Kanban label passed 2/2 test processes,
the core aggregate passed 22/22, the isolated public-header executable returned
success, and the same-cache Metal smoke passed. The two Windows-specific checks
above remain pending until the cross-platform build runs.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md` and the
`draxul-grid-host` phase of
`kanban/done/02 host-layer-static-libraries -refactor.md`. One Kanban owner can
complete this independently after those names are stable.

## Windows closeout (2026-09-22)

The final MSVC products aggregate passed 48/48, including Kanban filesystem and
SDL navigation coverage; same-cache Vulkan smoke passed.
