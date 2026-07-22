# Split Kanban core from grid-host integration

**Type:** refactor
**Priority:** P2 / sequence 04
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 5

## Goal

Keep board, storage, layout, and navigation in `draxul-kanban`, and move
`KanbanHost` plus provider registration into `draxul-kanban-host`.

## Boundary verification

- [ ] Confirm only `kanban_host.cpp`/`kanban_host.h` require `GridHostBase` and host registration.
- [ ] Inventory public header include needs for board, store, layout, and navigation.
- [ ] Verify SDL is needed only privately by navigation implementation.
- [ ] Split test inventory into pure core cases and host lifecycle/rendering cases.
- [ ] Compare the intended split with the existing Markdown core/host precedent.

## Implementation and migration

- [ ] Remove host sources from `draxul-kanban` without changing their directory initially.
- [ ] Add `draxul-kanban-host` for `KanbanHost` and provider registration.
- [ ] Link core only to required value/filesystem dependencies; keep SDL private.
- [ ] Link host to core plus `draxul-grid-host`/host API.
- [ ] Update executable provider linkage and test targets.
- [ ] Avoid splitting board/store/layout/navigation into additional micro-libraries.

## Unit tests

- [ ] Link board, store, layout, and navigation tests to `draxul-kanban` only.
- [ ] Keep `kanban_host_tests.cpp` on the host target and existing grid-host fixture.
- [ ] Add a Kanban core public-header/link-isolation build.
- [ ] Build `draxul-kanban`, `draxul-kanban-host`, and `draxul-test-markdown-kanban`.
- [ ] Run `ctest --test-dir build -C Release -L kanban --output-on-failure`.

## Cross-platform validation

- [ ] Exercise filesystem ordering/moves on Windows and macOS path semantics.
- [ ] Exercise SDL navigation key constants on both platforms.
- [ ] Confirm host rendering behaves the same through the existing grid contract.
- [ ] Confirm no Vulkan/Metal backend-specific API enters core.

## Agent documentation and tooling

- [ ] Update `docs/module-map.md` and any target examples naming Kanban.
- [ ] Ensure `python do.py test --label kanban` builds the host/core owner target correctly.

## Acceptance criteria

- [ ] Pure Kanban consumers no longer receive the full host/terminal/Nvim closure.
- [ ] Core and host tests link only their owning target boundaries.
- [ ] Host registration and user-visible board behavior are unchanged.
- [ ] Focused tests, full build, and smoke pass on available platforms.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md` and the
`draxul-grid-host` phase of
`kanban/pending/02 host-layer-static-libraries -refactor.md`. One Kanban owner can
complete this independently after those names are stable.
