# Move PCB selection policy into the CPU core

**Priority:** P2 — makes combined selection behavior testable without initializing ImGui or native rendering.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 9.  
**Owner:** One PCBView interaction/core agent.  
**Dependencies:** None among accepted refactors; independent of shared canvas adoption.

**Evidence:** `plugins/pcbview/src/pcbview_runtime.cpp:151–275` implements picking/highlight policy; `:308–338` defines mount → route → airwire precedence and clearing. Input requires initialized ImGui at `:277–282`; drawing consumes selection at `:527–559`. Existing `draxul-pcbview-core` depends only on GLM/private JSON, and router tests already cover primitive picking.

**Boundary:** Add selection state and pure selection/highlight functions to existing `draxul-pcbview-core`. Inputs are board/routes, visibility settings, world position, and tolerance; outputs are selected indices and highlight decisions. Runtime retains ImGui arbitration, screen/world conversion, camera operations, and drawing.

#### Boundary verification

- [x] Capture current hit precedence, nearest-hit tie behavior, tolerance, clearing, adjacency, and failure-focus rules.
- [x] Identify all runtime reads/writes of selected indices and selection-dependent drawing.
- [x] Keep router changes, including `kanban/pending/31 pcbview-routing-cell-budget -bug.md`, outside this extraction.

#### Implementation and migration

- [x] Introduce core selection state and pure queries consumed by the runtime.
- [x] Move hit selection and updates into core while keeping tolerance calculation and coordinate conversion in runtime.
- [x] Delegate drawing highlight queries to core.
- [x] Remove redundant runtime selection state after migration.

#### Unit tests

- [x] Extend `draxul-test-pcbview` for overlapping-hit precedence, deselection, adjacency, hidden layers, selected-versus-failure focus, and relevant boundary indices.
- [x] Preserve existing primitive route/via/failure tests.
- [x] Build with `cmake --build <cache> --config Debug --target draxul-test-pcbview --parallel`.
- [x] Run `ctest --test-dir <cache> -C Debug -R '^draxul-test-pcbview-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Confirm the core acquires no SDK, SDL, ImGui, NanoVG, Vulkan, or Metal dependency.
- [x] Preserve main-thread runtime ownership and identical world-space calculations on Windows/macOS.
- [x] Run `python3 do.py test debug --pcbview`, then `python3 do.py smoke debug --skip-build`.
- [x] Verify PCB render/interaction behavior on Metal; preserve existing selection visuals.
- [x] Verify PCB render/interaction behavior on Vulkan.

#### Agent documentation/tooling

- [x] Document selection ownership in PCBView’s module guidance.
- [x] Keep test additions in the existing product target and aggregate.
- [x] Commit in the PCBView repository (`6c20875`) and adopt its pointer deliberately in core.

#### Acceptance criteria

- [x] Combined selection policy is testable without graphics/UI initialization.
- [x] Runtime owns event adaptation and presentation, while core owns selection decisions.
- [x] Picking order, tolerance, highlighting, and failure-focus behavior remain unchanged.

Implementation note: `SelectionState` and all hit/highlight decisions now live
in the graphics-free core API. Runtime code retains ImGui arbitration,
screen-to-world conversion, pixel-derived tolerance, camera behavior, and
presentation. The focused PCB target and shard passed, followed by the 25-entry
core + PCBView aggregate and bounded same-cache smoke. The aggregate includes a
deterministic macOS Metal capture and the selection policy suite; the latter now
distinguishes routed-copper precedence from an overlapping airwire with a
different connection index. PCBView commit `6c20875` is pushed and adopted by
the parent repository. Windows/Vulkan validation remains pending.

## Windows closeout (2026-09-22)

The selection policy suite and PCBView Vulkan render passed in the final 48/48
products aggregate; same-cache smoke passed.
