# Isolate Rezonality audio capture and device operations

**Priority:** P2 — makes difficult device/lifecycle failures deterministic and removes native-renderer coupling from focused audio tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 4.  
**Owner:** One Rezonality audio agent; serialize product CMake edits with pipeline/runtime owners.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` for target policy; agree value ownership with `kanban/pending/80 rezonality-project-pipeline -refactor.md` and `kanban/pending/81 rezonality-runtime-backend-boundary -refactor.md`.

**Evidence:** `plugins/rezonality/src/audio_analysis.cpp:164–365` combines permission polling, device opening, subscriber visibility, buffering, teardown, and a static capture registry. Synthetic/Silent tests bypass capture (`tests/rezonality_plugin_contract_tests.cpp:348–378`). Product and test CMake duplicate audio compilation alongside native rendering. `src/live_project.h:3,30` also consumes `AudioOptions`.

**Boundary:** Product-private `draxul-rezonality-audio STATIC`, preserving `AudioAnalyzer` and its value contracts. Capture/device/permission operations, registry, synchronization, DSP, and native handles stay private. Dependencies are standard C++, SDL requirements, and platform permission support—not renderer, Assimp, compiler, or App. Keep option/value records independently includable.

#### Boundary verification

- [x] Freeze value/header ownership: `audio_types.h` keeps project-pipeline audio values independent of capture implementation and SDL.
- [x] Inventory exact-device sharing, visibility transitions, permission/open outcomes, generation semantics, and shutdown ordering.
- [x] Record Apple module versus executable SDL linkage before moving sources.

#### Implementation and migration

- [x] Move audio and selected permission sources into the static target; remove duplicate compilation.
- [x] Add the target to product `PRODUCT_TARGETS`, retaining PIC and product sanitizer/coverage/MSVC policy.
- [x] Add private injectable device, permission, and wait operations plus an explicitly owned test registry.
- [x] Retain production sharing and existing `AudioAnalyzer` callers.
- [x] Add proposed `draxul-test-rezonality-audio` with no renderer/App dependency.

#### Unit tests

- [x] Cover permission pending/granted/denied; initialization, device lookup, open, and resume failure.
- [x] Cover cancellation during opening, exactly-once cleanup, same/distinct-device sharing, and visibility reference counts.
- [x] Cover final-hidden pause/clear, backlog limits, and existing Synthetic/Silent outputs.
- [x] Retain real module/edit/reload and synthetic-audio render coverage.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-test-rezonality-audio --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-rezonality-audio-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Preserve Apple module header-only SDL linkage and host symbol resolution; test executables supply SDL implementation.
- [ ] Preserve ARC/AVFoundation permissions and Windows SDL/stub behavior.
- [x] Keep Vulkan/Metal analysis texture contents and generations unchanged.
- [x] Run `python3 do.py test debug --rezonality`, then `python3 do.py smoke debug --skip-build`. (macOS: focused audio 6/6 cases and 55 assertions; final core + Rezonality aggregate 29/29 CTest entries in 106.53s; same-cache Metal startup smoke passed.)
- [x] Leave unavailable manual microphone and Metal golden checks explicitly pending.

#### Agent documentation/tooling

- [x] Register the focused suite in product aggregates, `do.py` patterns, and runner selection tests.
- [x] Preserve absent-product conditional registration and classify moved tests once.
- [x] Update Rezonality guidance with the value-only audio boundary and adopt Rezonality commit `0a96351` in the parent repository.

#### Acceptance criteria

- [x] Audio implementation has one library owner.
- [x] Capture failure/lifecycle tests require neither physical input nor App/native rendering.
- [x] The CPU project target remains independent of SDL implementation.
- [x] Existing sharing, visibility, shutdown, module loading, and rendered audio behavior remain compatible.
