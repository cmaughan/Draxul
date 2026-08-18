# Extract the pure App-shell library

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude and Grok  
**Depends on:** `kanban/pending/00 internal-target-build-policy -refactor.md` and
`kanban/pending/01 app-local-cmake-ownership -refactor.md`

## Boundary verification

- [ ] Inventory callers and dependencies of split tree, shell/chrome layout, pill, rename, and fuzzy matching.
- [ ] Verify `PaneDescriptor` and `SystemResourceSnapshot` are neutral value records.
- [ ] Capture current App test classification and public include paths.
- [ ] Confirm `draxul-app-support` has no consumer beyond core tests.

## Implementation and migration

- [ ] Move the two neutral records to `draxul-types`, with direct include migration and no forwarding duplicates.
- [ ] Add `draxul-app-shell` and public headers.
- [ ] Move fuzzy/shell layout, split tree, then chrome/pill/rename sources.
- [ ] Link `draxul-app` to the new target.
- [ ] Add `draxul-test-app-shell` and label.
- [ ] Retire `draxul-app-support` and replace its sole consumer with explicit links.

## Unit tests

- [ ] Move split, shell-layout, chrome-layout, fuzzy, and rename state tests.
- [ ] Add a public-header/link-isolation consumer.
- [ ] Build only `draxul-app-shell` and `draxul-test-app-shell`; run label `app-shell`.

## Cross-platform validation

- [ ] Compare deterministic results on MSVC and Apple Clang.
- [ ] Verify no renderer/window/host/Nvim/NanoVG/ImGui link enters the target.
- [ ] Run App tests and render smoke on both backends.

## Agent documentation/tooling

- [ ] Add dependency-contract comments.
- [ ] Update the module map and
      `kanban/pending/08 agent-guidance-label-validation -refactor.md` validation mapping.

## Acceptance criteria

- [ ] Pure shell tests build without the App/product/GPU closure.
- [ ] `draxul-app-support` is removed.
- [ ] App behavior, geometry, and visuals are unchanged.
- [ ] Full build, tests, render snapshots, and smoke remain green.
