# Share NanoVG’s CPU paint conversion

**Priority:** P2 — removes identical platform arithmetic and permits device-free characterization.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 10.  
**Owner:** One NanoVG CPU/backend agent.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` for focused tests. Independent of shared plugin adapters; serialize backend CMake edits.

**Evidence:** `libs/draxul-nanovg/backend/src/nanovg_vk.cpp:264–370` and `nanovg_mtl.mm:216–322` duplicate transform packing, premultiplication, scissor conversion, image flipping, texture selection, and stroke factors. Native context access only supplies texture metadata. Existing synchronization source checks do not test this arithmetic.

**Boundary:** Private plain-C++ conversion helper within `draxul-nanovg-backend`, accepting paint/scissor values, stroke parameters, and optional texture metadata. Return equivalent uniform values; native adapters retain texture lookup, allocation, recording, and retirement. No new production library or public SDK interface.

#### Boundary verification

- [x] Compare operation order, enum values, native record offsets, zeroed padding, and 256-byte layouts.
- [x] Distinguish no image from a requested image whose texture lookup failed.
- [x] Preserve current partial output on missing texture metadata.
- [x] Keep shader schema/reflection work in `kanban/ice-box/19 shader-abi-parity -test.md`.

#### Implementation and migration

- [x] Characterize current outputs before extraction.
- [x] Add the private CPU helper and minimal native record adaptation.
- [x] Migrate Vulkan, then Metal, preserving arithmetic order and existing binding layouts.
- [x] Remove duplicated conversion code after both callers use the helper.

#### Unit tests

- [x] Add `draxul-test-nanovg-paint` for transformed/disabled scissors, gradients, RGBA premultiplication modes, alpha images, Y flip, stroke values, missing metadata, and matrix packing.
- [x] Check native adapter layout compatibility without expanding into shader-generation work.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-test-nanovg-paint --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-nanovg-paint-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Build the backend and app on Windows/Vulkan and macOS/Metal.
- [x] Preserve Objective-C++/ARC compilation settings when adding plain C++ sources.
- [x] Verify CPU tests need no device; document any remaining native SDK configure/link requirements.
- [ ] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`.
- [ ] Run shared NanoVG and affected product render coverage on both backends.

#### Agent documentation/tooling

- [x] Register focused tests in core aggregates, `do.py`, and selection tests.
- [x] Document ownership of shared arithmetic versus native resources and submission.
- [ ] Keep copied-tree NanoVG backend builds compatible.

#### Acceptance criteria

- [x] Paint conversion arithmetic has one implementation.
- [x] Uniform layout, zero initialization, missing-texture behavior, and floating-point operation order remain compatible.
- [ ] Device-free tests cover conversion semantics and native rendering remains equivalent.
