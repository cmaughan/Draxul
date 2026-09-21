# Preserve route-pair identity when failed routes are removed

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`plugins/megacity/product/draxul-megacity/src/semantic_city_layout.cpp:1172` compacts successful routes, but `:1258` indexes the original pair vector with the compacted index. An unroutable pair followed by a valid route can assign elevations from the wrong buildings.

**Investigation**

- [ ] Construct a failed route followed by successful routes with distinct layer heights.

**Fix strategy**

- [ ] Retain each successful result’s original pair identity through compaction.
- [ ] Compute endpoint elevations from that retained association.

**Acceptance criteria**

- [ ] Every route receives elevations from its own endpoints.
- [ ] Skipping failed routes preserves deterministic ordering and existing valid layouts.
