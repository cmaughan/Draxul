# Extract the pure App-shell library

**Type:** refactor  
**Priority:** P1  
**Raised by:** Claude and Grok  
**Depends on:** pending `00` and `01`

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
- [ ] Update module map and pending `08` label mapping.

## Acceptance criteria

- [ ] Pure shell tests build without the App/product/GPU closure.
- [ ] `draxul-app-support` is removed.
- [ ] App behavior, geometry, and visuals are unchanged.
- [ ] Full build, tests, render snapshots, and smoke remain green.
