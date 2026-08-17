# Move `draxul-app` build ownership into `app/`

**Type:** refactor
**Priority:** P2 / sequence 01
**Raised by:** Claude
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 2

## Goal

Create `app/CMakeLists.txt` for the existing `draxul-app` library while keeping
the executable, packaging, generic plugin staging, and assets root-owned.

## Boundary verification

- [ ] Capture the current `draxul-app` source list, include directories, compile
  definitions, PUBLIC/PRIVATE links, and platform sources from root CMake.
- [ ] Separate library-only platform input (`macos_menu.mm`) from executable
  resources (`draxul.rc`, macOS icon/bundle assets).
- [ ] Record generated target/link information for comparison after the move.
- [ ] Confirm no existing card already owns this exact CMake relocation.

## Implementation and migration

- [ ] Add `app/CMakeLists.txt` defining only `draxul-app` and its local policy call.
- [ ] Replace the root library block with `add_subdirectory(app)`.
- [ ] Keep target name, source paths, include surface, compile definitions, and links unchanged.
- [ ] Keep `draxul`, product registration links, install/copy rules, icons, and resources in root CMake.
- [ ] Make the move mechanical; do not mix dependency cleanup or C++ source refactors.

## Unit and build tests

- [ ] Configure through the normal `do.py` Debug workflow, then use the generated cache
      for focused target comparisons.
- [ ] Build `draxul-app`, `draxul-test-app`, and `draxul` in Release.
- [ ] Run `ctest --test-dir build -C Release -L app --output-on-failure`.
- [ ] Compare generated target sources and link closure with the pre-move inventory.

## Cross-platform validation

- [ ] Windows: verify `app/draxul.rc` remains attached to the executable.
- [ ] macOS: verify `app/macos_menu.mm`, bundle properties, icon, and resources remain attached correctly.
- [ ] Configure with each mounted plugin enabled, disabled, and absent; preserve generic
      registration/staging without product-specific app links.
- [ ] Confirm Vulkan/Metal selection remains wholly owned by renderer CMake.

## Agent documentation and tooling

- [ ] Update `docs/module-map.md` only if it currently claims root owns the app library target.
- [ ] Add a short app build-ownership note to canonical guidance if useful; do not duplicate source lists.

## Acceptance criteria

- [ ] Root CMake contains `add_subdirectory(app)` and no `draxul-app` source list.
- [ ] The generated target and executable payload are equivalent to the pre-move build.
- [ ] Focused app tests, full build, and smoke pass.
- [ ] No production source or behavior changes are included.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md` so the
new local target uses the final policy helper. One app/build agent owns this
two-file migration; do not parallelize edits to root CMake.
