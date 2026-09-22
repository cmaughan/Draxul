# Bound PCB routing dimensions before arithmetic and allocation

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/pcbview/src/autorouter.cpp:224` narrows computed dimensions, and `:230` multiplies them as `int`. Positive finite dimensions accepted at `board_model.cpp:115` can overflow or allocate gigabytes; initialization exceptions escape `pcbview_plugin.cpp:96`.

**Investigation**

- [x] Evaluate extreme board dimensions, fine grid spacing, and layer counts.

**Fix strategy**

- [x] Check conversions and multiplication before narrowing.
- [x] Enforce a documented total routing-memory/cell budget.
- [x] Contain initialization failures at the plugin boundary.

**Acceptance criteria**

- [x] Oversized inputs fail diagnostically before overflow or excessive allocation.
- [x] Supported boards still route, and a rejected board cannot terminate Draxul.

**Validation follow-up**

- [x] Pass the same-cache smoke and confirm the plugin boundary on Windows/Vulkan.

Implementation note: the router now validates columns, rows, and their
layer-multiplied total against a 2,000,000-cell budget before narrowing or any
grid-sized allocation. JSON loading reuses the same check, and the plugin ABI
creation boundary logs and rejects both standard and unknown initialization
exceptions. Source and regression tests are prepared in the PCBView repository;
the plugin creation boundary compiles with exception containment. The focused
PCBView build and test shard passed, as did the 11-test core + PCBView aggregate.
The same-cache smoke remains unsuccessful: Metal initialized, then the smoke
client hung for more than three minutes despite its 3-second deadline and had
to be stopped by exact PID. The clientless server it spawned was shut down
gracefully. Cross-platform validation remains pending.

## Windows closeout (2026-09-22)

The final Windows products aggregate passed 48/48, including the PCBView Vulkan
render and guarded plugin boundary; the same-cache smoke passed.
