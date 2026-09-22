# Extract Rezonality’s CPU project pipeline

**Priority:** P2 — isolates expensive native dependencies and makes parser/compiler failures deterministic to test.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 2.  
**Owner:** One Rezonality pipeline agent; coordinate candidate records with the runtime owner.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md`. Contract prerequisite for `kanban/pending/81 rezonality-runtime-backend-boundary -refactor.md`.

**Evidence:** `plugins/rezonality/src/live_project.cpp:454–534` combines scene reading, parsing, and model loading; `:725–838` combines compiler execution and diagnostics; `:1083–1264` combines candidate construction with scheduling/publication. `LiveProject::build()` is private in `src/live_project.h:129–141`. Product and test CMake compile CPU sources alongside native implementation and app dependencies.

**Boundary:** Product-private `draxul-rezonality-project STATIC`, consumed by the plugin and focused CPU tests. Expose product-internal scene-text parsing and synchronous candidate construction returning typed values/diagnostics. Keep compiler execution, asset loading, JSON, Assimp, and stb private; expose GLM only where public value records require it. `LiveProject` remains the scheduling/publication wrapper. No SDL implementation, renderer, app, or plugin ABI dependency.

#### Boundary verification

- [x] Inventory candidate values, audio-related value records, CPU sources, compile definitions, and direct dependencies.
- [x] Keep `AudioOptions` and other audio value records independently includable: `plugins/rezonality/src/live_project.h` consumes the SDL-free `audio_types.h` boundary without linking to capture implementation.
- [x] Freeze immutable candidate ownership and explicit per-build temporary-directory context with the runtime owner.
- [x] Coordinate completed `kanban/done/07 rezonality-worker-exception-recovery -bug.md` and `kanban/done/42 rezonality-image-storage-format -bug.md`.

#### Implementation and migration

- [x] Move existing CPU sources into the static target first; link the module/tests to it and remove duplicate CPU source compilation.
- [x] Register the target in product `PRODUCT_TARGETS`; preserve PIC and product-owned sanitizer/coverage/MSVC policy.
- [ ] Expose synchronous `parse_scene_text(...)` and `build_candidate(...)` seams with injectable compiler results.
- [ ] Separate syntax parsing from model/image resolution while preserving declaration order, path resolution, and diagnostic provenance.
- [x] Preserve per-instance temporary-output isolation and existing watch/debounce publication.

#### Unit tests

- [ ] Add `draxul-test-rezonality-project` for malformed scenes, missing assets, model ordering, compiler diagnostics, empty SPIR-V, and candidate ownership.
- [x] Retain real compiler, watcher, dynamic-module, and repair tests.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-test-rezonality-project --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-rezonality-project-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Preserve Windows compiler quoting and Apple’s `REZONALITY_METAL_SEPARATE_MODEL_SAMPLER` argument.
- [x] Verify the CPU target excludes Vulkan/Metal implementation links; preserve native shader translation in its existing owner.
- [ ] Run `python3 do.py test debug --rezonality`, then `python3 do.py smoke debug --skip-build`; verify corresponding Windows coverage.
- [x] Keep unavailable Metal goldens/manual checks explicitly pending.

#### Agent documentation/tooling

- [x] Register the focused target in the Rezonality aggregate, `do.py` selection, and runner tests; keep absent-product behavior conditional.
- [x] Update product guidance with worker-built immutable candidates and render-thread activation responsibilities.
- [ ] Commit implementation in the Rezonality repository and adopt its pointer deliberately in core.

#### Acceptance criteria

- [x] CPU tests build and execute without the application or native renderer target.
- [x] Production CPU implementation has one library owner.
- [x] Existing project syntax, compiler arguments, reload behavior, and diagnostics remain compatible.
- [x] Each migration step remains buildable.
