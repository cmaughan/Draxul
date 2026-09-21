# Move PCB selection policy into the CPU core

**Priority:** P2 — makes combined selection behavior testable without initializing ImGui or native rendering.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 9.  
**Owner:** One PCBView interaction/core agent.  
**Dependencies:** None among accepted refactors; independent of shared canvas adoption.

**Evidence:** `plugins/pcbview/src/pcbview_runtime.cpp:151–275` implements picking/highlight policy; `:308–338` defines mount → route → airwire precedence and clearing. Input requires initialized ImGui at `:277–282`; drawing consumes selection at `:527–559`. Existing `draxul-pcbview-core` depends only on GLM/private JSON, and router tests already cover primitive picking.

**Boundary:** Add selection state and pure selection/highlight functions to existing `draxul-pcbview-core`. Inputs are board/routes, visibility settings, world position, and tolerance; outputs are selected indices and highlight decisions. Runtime retains ImGui arbitration, screen/world conversion, camera operations, and drawing.

#### Boundary verification

- [ ] Capture current hit precedence, nearest-hit tie behavior, tolerance, clearing, adjacency, and failure-focus rules.
- [ ] Identify all runtime reads/writes of selected indices and selection-dependent drawing.
- [ ] Keep router changes, including `kanban/pending/31 pcbview-routing-cell-budget -bug.md`, outside this extraction.

#### Implementation and migration

- [ ] Introduce core selection state and pure queries behind runtime forwarding methods.
- [ ] Move hit selection and updates into core while keeping tolerance calculation and coordinate conversion in runtime.
- [ ] Delegate drawing highlight queries to core.
- [ ] Remove redundant runtime selection state after migration.

#### Unit tests

- [ ] Extend `draxul-test-pcbview` for overlapping-hit precedence, deselection, adjacency, hidden layers, selected-versus-failure focus, and relevant boundary indices.
- [ ] Preserve existing primitive route/via/failure tests.
- [ ] Build with `cmake --build <cache> --config Debug --target draxul-test-pcbview --parallel`.
- [ ] Run `ctest --test-dir <cache> -C Debug -R '^draxul-test-pcbview-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Confirm the core acquires no SDK, SDL, ImGui, NanoVG, Vulkan, or Metal dependency.
- [ ] Preserve main-thread runtime ownership and identical world-space results on Windows/macOS.
- [ ] Run `python3 do.py test debug --pcbview`, then `python3 do.py smoke debug --skip-build`.
- [ ] Verify PCB render/interaction behavior on Vulkan and Metal; preserve existing selection visuals.

#### Agent documentation/tooling

- [ ] Document selection ownership in PCBView’s module guidance.
- [ ] Keep test additions in the existing product target and aggregate.
- [ ] Commit in the PCBView repository and adopt its pointer deliberately in core.

#### Acceptance criteria

- [ ] Combined selection policy is testable without graphics/UI initialization.
- [ ] Runtime owns event adaptation and presentation, while core owns selection decisions.
- [ ] Picking order, tolerance, highlighting, and failure-focus behavior remain unchanged.
