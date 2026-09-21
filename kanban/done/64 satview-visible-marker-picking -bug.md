# Apply identical visibility and limits to marker drawing and picking

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/runtime/satview_runtime.cpp:871` applies the drawing cap after horizon rejection, while picking applies it at `:4962` before that rejection. With a finite limit and early below-horizon objects, visible later markers cannot be selected.

**Investigation**

- [x] Build a ground-view snapshot with occluded objects preceding visible markers near the limit.

**Fix strategy**

- [x] Share marker eligibility and limit ordering between rendering and picking.
- [x] Preserve the selected-marker exception explicitly.

**Acceptance criteria**

- [x] Every displayed eligible marker can be picked at its rendered position.
- [x] Occluded markers do not consume the visible-marker budget.
- [x] Unlimited, map, and existing selection behavior remain correct.
