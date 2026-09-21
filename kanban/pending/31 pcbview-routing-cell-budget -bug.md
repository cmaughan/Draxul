# Bound PCB routing dimensions before arithmetic and allocation

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/pcbview/src/autorouter.cpp:224` narrows computed dimensions, and `:230` multiplies them as `int`. Positive finite dimensions accepted at `board_model.cpp:115` can overflow or allocate gigabytes; initialization exceptions escape `pcbview_plugin.cpp:96`.

**Investigation**

- [ ] Evaluate extreme board dimensions, fine grid spacing, and layer counts.

**Fix strategy**

- [ ] Check conversions and multiplication before narrowing.
- [ ] Enforce a documented total routing-memory/cell budget.
- [ ] Contain initialization failures at the plugin boundary.

**Acceptance criteria**

- [ ] Oversized inputs fail diagnostically before overflow or excessive allocation.
- [ ] Supported boards still route, and a rejected board cannot terminate Draxul.
