# Extract the pure App-shell library

**Type:** refactor
**Priority:** P1
**Raised by:** Claude and Grok
**Depends on:** `kanban/pending/00 internal-target-build-policy -refactor.md` and
`kanban/pending/01 app-local-cmake-ownership -refactor.md`

## Boundary verification

- [x] Inventory callers and dependencies of split tree, shell/chrome layout, pill, rename, and fuzzy matching.
- [x] Verify `PaneDescriptor` and `SystemResourceSnapshot` are neutral value records.
- [x] Capture current App test classification and public include paths.
- [x] Confirm `draxul-app-support` has no consumer beyond core tests.

## Implementation and migration

- [x] Move the two neutral records to `draxul-types`, with direct include migration and no forwarding duplicates.
- [x] Add `draxul-app-shell` and public headers.
- [x] Move fuzzy/shell layout, split tree, then chrome/pill/rename sources.
- [x] Link `draxul-app` to the new target.
- [x] Add `draxul-test-app-shell` and label.
- [x] Retire `draxul-app-support` and replace its sole consumer with explicit links.

## Unit tests

- [x] Move split, shell-layout, chrome-layout, fuzzy, and rename state tests.
- [x] Add a public-header/link-isolation consumer.
- [x] Build only `draxul-app-shell` and `draxul-test-app-shell`; run label `app-shell`.

## Cross-platform validation

- [x] Apple Clang deterministic results match the pinned shell geometry/state fixtures.
- [ ] Compare the same deterministic results on MSVC.
- [x] Verify no renderer/window/host/Nvim/NanoVG/ImGui link enters the target.
- [x] Run Apple Clang App tests and Metal startup smoke.
- [ ] Run App tests and render smoke on MSVC/Vulkan; that runtime was unavailable.

## Agent documentation/tooling

- [x] Add dependency-contract comments.
- [x] Update the module map and
      `kanban/pending/08 agent-guidance-label-validation -refactor.md` validation mapping.

## Acceptance criteria

- [x] Pure shell tests build without the App/product/GPU closure.
- [x] `draxul-app-support` is removed.
- [x] App behavior, geometry, and visuals are unchanged.
- [x] Full build, tests, and smoke remain green. This boundary-only move did not
      require new render snapshots under the canonical validation guidance.

## Validation record

- `draxul-app-shell`, `draxul-test-app-shell`, and its public-header isolation
  consumer build independently.
- Focused `app-shell|host-api` CTest selection passed 3/3.
- Apple Clang Debug full build passed; core aggregate passed 14/14; same-cache
  Metal startup smoke passed. MSVC/Vulkan runtime coverage was unavailable in
  this workspace.
