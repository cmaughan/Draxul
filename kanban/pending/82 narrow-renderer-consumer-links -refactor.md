# Replace unnecessary concrete renderer dependencies

**Priority:** P2 — small dependency changes improve link isolation with limited implementation churn.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 3.  
**Owner:** One core dependency/build agent.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` for focused test targets. No dependency on another accepted refactor; coordinate NanoVG CMake edits.

**Evidence:** Concrete renderer links remain in `libs/draxul-gui/CMakeLists.txt:20–25`, `libs/draxul-ui/CMakeLists.txt:17–20`, and `libs/draxul-nanovg/CMakeLists.txt:89–90`. UI uses `IImGuiHost`/`IFrameContext`; NanoVG uses render contracts already provided by `draxul-plugin-render-support`. The renderer adds window/SDL/ImGui closure.

**Boundary:** Remove GUI’s renderer link. Replace UI’s private renderer edge and core NanoVG’s public renderer edge with existing render support, retaining explicitly required font, tooltip, ImGui, SDL, backend, and native dependencies. No new production library or public API.

#### Boundary verification

- [x] Inventory includes and symbols used by all three consumers; capture direct and transitive links.
- [x] Confirm public headers receive their contracts directly rather than through unrelated consumers.
- [x] Verify Vulkan’s VMA implementation currently arrives transitively and must remain supplied explicitly.

#### Implementation and migration

- [x] Remove GUI’s renderer edge and validate that consumer first.
- [x] Replace UI’s renderer link with private render support, retaining explicit ImGui/SDL requirements.
- [x] Replace NanoVG’s renderer link with public render support and explicitly retain `draxul-vulkan-resources` on Vulkan.
- [x] Preserve Metal frameworks/ARC and existing native source selection.

#### Unit tests

- [x] Create narrowly linked contract consumers/focused tests, including `ui_panel_backend_tests.cpp` and relevant overlay/layout cases.
- [x] Ensure the test closure does not reintroduce the concrete renderer indirectly.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-gui draxul-ui draxul-nanovg draxul-test-render-contracts --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-render-contracts-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Verify macOS retains its Metal framework/ARC behavior.
- [ ] Verify Windows Vulkan linking has one VMA implementation (requires a Windows host).
- [x] Build the macOS app and verify panel/NanoVG rendering on Metal.
- [ ] Build the Windows app and verify panel/NanoVG rendering on Vulkan (requires a Windows host).
- [x] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`; run affected registered render cases not included in that aggregate.

#### Agent documentation/tooling

- [x] Register focused tests in core aggregates, `do.py`, and selection tests.
- [x] Correct dependency comments in GUI/UI documentation, NanoVG CMake, and the module map.
- [x] Document reduced target closure without claiming GPU-independent root configuration.

#### Acceptance criteria

- [x] None of the three libraries directly or transitively depends on the concrete renderer.
- [x] Contract tests do not rely on the broad core test target to mask missing links.
- [x] Existing interfaces, visuals, and macOS Metal linkage remain compatible.
- [ ] Windows Vulkan visuals/linkage remain compatible (requires a Windows host).

#### Validation evidence (macOS Metal)

- Exact GUI/UI/NanoVG, isolation-consumer, and focused-contract target build passed.
- Focused render-contract shard passed 1/1; products aggregate passed 41/41.
- Same-cache smoke passed in 6.51 seconds on Apple M5.
- Panel render comparison passed; NanoVG comparison matched its reference exactly
  (0/912000 changed pixels).
