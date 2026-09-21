# Isolate Rezonality audio capture and device operations

**Priority:** P2 — makes difficult device/lifecycle failures deterministic and removes native-renderer coupling from focused audio tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 4.  
**Owner:** One Rezonality audio agent; serialize product CMake edits with pipeline/runtime owners.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` for target policy; agree value ownership with `kanban/pending/80 rezonality-project-pipeline -refactor.md` and `kanban/pending/81 rezonality-runtime-backend-boundary -refactor.md`.

**Evidence:** `plugins/rezonality/src/audio_analysis.cpp:164–365` combines permission polling, device opening, subscriber visibility, buffering, teardown, and a static capture registry. Synthetic/Silent tests bypass capture (`tests/rezonality_plugin_contract_tests.cpp:348–378`). Product and test CMake duplicate audio compilation alongside native rendering. `src/live_project.h:3,30` also consumes `AudioOptions`.

**Boundary:** Product-private `draxul-rezonality-audio STATIC`, preserving `AudioAnalyzer` and its value contracts. Capture/device/permission operations, registry, synchronization, DSP, and native handles stay private. Dependencies are standard C++, SDL requirements, and platform permission support—not renderer, Assimp, compiler, or App. Keep option/value records independently includable.

#### Boundary verification

- [ ] Freeze value/header ownership so the CPU project pipeline does not link audio implementation or SDL.
- [ ] Inventory exact-device sharing, visibility transitions, permission/open outcomes, generation semantics, and shutdown ordering.
- [ ] Record Apple module versus executable SDL linkage before moving sources.

#### Implementation and migration

- [ ] Move audio and selected permission sources into the static target; remove duplicate compilation.
- [ ] Add the target to product `PRODUCT_TARGETS`, retaining PIC and product sanitizer/coverage/MSVC policy.
- [ ] Add private injectable device, permission, and wait operations plus an explicitly owned test registry.
- [ ] Retain production sharing and existing `AudioAnalyzer` callers.
- [ ] Add proposed `draxul-test-rezonality-audio` with no renderer/App dependency.

#### Unit tests

- [ ] Cover permission pending/granted/denied; initialization, device lookup, open, and resume failure.
- [ ] Cover cancellation during opening, exactly-once cleanup, same/distinct-device sharing, and visibility reference counts.
- [ ] Cover final-hidden pause/clear, backlog limits, and existing Synthetic/Silent outputs.
- [ ] Retain real module/edit/reload and synthetic-audio render coverage.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-test-rezonality-audio --parallel`.
- [ ] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-rezonality-audio-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve Apple module header-only SDL linkage and host symbol resolution; test executables supply SDL implementation.
- [ ] Preserve ARC/AVFoundation permissions and Windows SDL/stub behavior.
- [ ] Keep Vulkan/Metal analysis texture contents and generations unchanged.
- [ ] Run `python3 do.py test debug --rezonality`, then `python3 do.py smoke debug --skip-build`.
- [ ] Leave unavailable manual microphone and Metal golden checks explicitly pending.

#### Agent documentation/tooling

- [ ] Register the focused suite in product aggregates, `do.py` patterns, and runner selection tests.
- [ ] Preserve absent-product conditional registration and classify moved tests once.
- [ ] Update Rezonality guidance/module map; commit product changes separately and adopt the pointer deliberately.

#### Acceptance criteria

- [ ] Audio implementation has one library owner.
- [ ] Capture failure/lifecycle tests require neither physical input nor App/native rendering.
- [ ] The CPU project target remains independent of SDL implementation.
- [ ] Existing sharing, visibility, shutdown, module loading, and rendered audio behavior remain compatible.
