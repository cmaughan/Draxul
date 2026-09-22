# Move `draxul-app` build ownership into `app/`

**Type:** refactor
**Priority:** P2 / sequence 01
**Raised by:** Claude
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 2

## Goal

Create `app/CMakeLists.txt` for the existing `draxul-app` library while keeping
the executable, packaging, generic plugin staging, and assets root-owned.

## Boundary verification

- [x] Capture the current `draxul-app` source list, include directories, compile
  definitions, PUBLIC/PRIVATE links, and platform sources from root CMake.
- [x] Separate library-only platform input (`macos_menu.mm`) from executable
  resources (`draxul.rc`, macOS icon/bundle assets).
- [x] Record generated target/link information for comparison after the move. The
      current CMake File API reports 28 `draxul-app` sources (including the two
      macOS Objective-C++ files), while the executable retains `main.cpp` plus
      its icon/resource payload. Product targets are staging dependencies and
      do not enter the app library's implementation link closure.
- [x] Confirm no existing card already owns this exact CMake relocation.

## Implementation and migration

- [x] Add `app/CMakeLists.txt` defining only `draxul-app` and its local policy call.
- [x] Replace the root library block with `add_subdirectory(app)`.
- [x] Keep target name, source paths, include surface, compile definitions, and links unchanged.
- [x] Keep `draxul`, product registration links, install/copy rules, icons, and resources in root CMake.
- [x] Make the move mechanical; do not mix dependency cleanup or C++ source refactors.

## Unit and build tests

- [x] Configure through the normal `do.py` Debug workflow, then use the generated cache
      for focused target comparisons.
- [x] Build `draxul-app`, `draxul-test-app`, and `draxul` in Release.
- [x] Run `ctest --test-dir build -C Release -L app --output-on-failure` (4/4 passed).
- [x] Compare generated target sources and link closure with the pre-move inventory.
      The mechanical `3837469^..3837469` relocation preserves the captured
      source, include, definition, platform-source, and link declarations; the
      current generated graph confirms the same ownership split after later
      library extractions.

## Cross-platform validation

- [x] Windows: verify `app/draxul.rc` remains attached to the executable.
- [x] macOS: verify `app/macos_menu.mm`, bundle properties, icon, and resources remain attached correctly.
- [x] Configure with each mounted plugin enabled, disabled, and absent; preserve generic
      registration/staging without product-specific app links.
- [x] Confirm Vulkan/Metal selection remains wholly owned by renderer CMake.

## Agent documentation and tooling

- [x] Confirm `docs/module-map.md` does not claim root owns the app library target; no correction was needed.
- [x] Keep the canonical `CLAUDE.md` ownership note (`app/`: executable and `draxul-app` orchestration target) without duplicating source lists.

## Acceptance criteria

- [x] Root CMake contains `add_subdirectory(app)` and no `draxul-app` source list.
- [x] The generated target and executable payload are equivalent to the pre-move build.
- [x] Focused app tests, full Release build, and Release smoke pass.
- [x] No production source or behavior changes are included in the relocation.

## Validation

- macOS Debug configured successfully through the normal `do.py` workflow and
  supplied the post-move CMake File API target graph.
- The full Release build and explicit `draxul-app`, `draxul-test-app`, and
  `draxul` target build passed. Release app-labeled CTest passed 4/4 with local
  socket access, followed by a successful Release startup smoke.
- Isolated configurations with all five mounted products enabled, all disabled,
  and enabled with all source directories absent generated successfully. The
  absent case emitted the expected continue-without-product status for each
  mount, while app links remained generic.
- The restored normal Debug validation passed all 45 unit CTest entries,
  startup smoke, and all five default Metal render comparisons.

## Dependencies and ownership

Depends on `kanban/pending/00 internal-target-build-policy -refactor.md` so the
new local target uses the final policy helper. One app/build agent owns this
two-file migration; do not parallelize edits to root CMake.
