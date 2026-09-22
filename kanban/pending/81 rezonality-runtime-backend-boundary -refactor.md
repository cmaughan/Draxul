# Separate Rezonality runtime policy from native rendering

**Priority:** P1 — duplicated activation policy and mixed resource ownership make lifecycle changes unusually risky.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 1.  
**Owner:** One Rezonality runtime owner; delegate native moves only after freezing ownership contracts.  
**Dependencies:** `kanban/pending/80 rezonality-project-pipeline -refactor.md`; `kanban/pending/00 internal-target-build-policy -refactor.md` for focused test registration.

**Evidence:** `plugins/rezonality/src/rezonality_plugin.cpp:2765–2793` combines project, audio, diagnostics, camera, candidate, presentation, and native state. Metal/Vulkan duplicate activation/failure policy at `:3159–3209` and `:3460–3512`. Product CMake compiles the entire entrypoint as Objective-C++ on Apple; tests recompile that native implementation.

**Boundary:** Thin C ABI adapter, private controller, and platform-selected native implementation files within the existing module. Controller owns CPU candidate state, activation outcomes, camera/audio orchestration, and diagnostic/presentation policy. Backend owns preparation, compatibility checks, recording, native resources, and completed-frame-slot retirement. Internal immutable build/frame inputs and typed preparation outcomes suffice; no public SDK expansion.

#### Boundary verification

- [x] Define candidate ownership, `needs_prepare`, prepare/record/retire responsibilities, and failure outcomes.
- [x] Keep audio orchestration in the controller through `AudioAnalyzer`; device opening, permissions, capture sharing, buffering, and teardown belong to the audio target proposed in `kanban/pending/93 rezonality-audio-capture-boundary -refactor.md`.
- [x] Serialize shared product CMake edits with the project-pipeline and audio owners after agreeing their value/header contracts.
- [x] Preserve Vulkan target-generation/render-pass checks and Metal format checks as distinct native requirements.
- [x] Land `kanban/pending/09 rezonality-pending-build-lifetime -bug.md` and the transactional resource fixes from `kanban/pending/48 rezonality-gpu-initialization-rollback -bug.md` before relocating affected logic.
- [x] Preserve the separate scope of `kanban/done/43 rezonality-diagnostic-utf8 -bug.md`.

#### Implementation and migration

- [x] Mechanically move native implementation into selected Vulkan `.cpp` and Metal `.mm` files.
- [x] Extract a controller that can be compiled independently for fake-backend tests; keep native dependencies out of its interface.
- [x] Centralize candidate selection, activation outcomes, status publication, and presentation notifications.
- [x] Delegate ABI callbacks after mechanical moves pass; retain existing audio/service adapters.
- [x] Update the source-location assertion in `rezonality_plugin_contract_tests.cpp:199–210` when Metal code moves.

#### Unit tests

- [x] Add `draxul-test-rezonality-runtime` covering prepare success/failure, resize recreation, attempted/active generations, hidden/quiesced state, and retirement ordering.
- [x] Retain actual module load/edit/break/repair coverage.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-test-rezonality-runtime --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-rezonality-runtime-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Preserve borrowed command-buffer ownership, Metal object lifetime, and retirement only after all using frame slots complete.
- [x] Preserve last-good-generation behavior without adding backend submit, present, or device-idle operations.
- [x] Run `python3 do.py test debug --target draxul-test-rezonality-runtime`, the elevated core plus Rezonality aggregate, and `python3 do.py smoke debug --skip-build` on macOS. The focused runtime suite passed 5 cases with 66 assertions, the aggregate passed 30/30 tests, and the same-cache Metal smoke passed.
- [ ] Run equivalent Windows/Vulkan validation.
- [x] Retain raster/PBR/ray and synthetic-audio scenarios; leave unavailable native checks pending.

#### Agent documentation/tooling

- [x] Register focused tests in product aggregates, `do.py`, and runner selection tests.
- [x] Document controller/backend ownership and native compatibility differences in product guidance.
- [x] Commit product implementation separately and adopt the parent pointer deliberately
      (`draxul-rezonality` `4674f24`, adopted by this root checkpoint).

#### Acceptance criteria

- [x] Activation/failure policy has one controller implementation.
- [x] Native handles remain private and ABI compatibility is unchanged.
- [x] Controller tests require neither native rendering nor the app.
- [x] Real reload recovery and supported native rendering remain intact.
