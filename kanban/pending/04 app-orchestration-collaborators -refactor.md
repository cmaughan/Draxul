# Give App’s projection, mutation and command effects explicit owners

**Priority:** P1 — `app/app.cpp` is a shared collision point with coupled state and callbacks.  
**Source:** `app/app.cpp`  
**Proposed by:** Claude 3, 14. **Owner:** one app agent; separate command work only after ports land. **Depends on:** card 03 for clean launch ownership.  
**Evidence:** remote reconcile, local mutation, agent launch, checkpoint effects and control effects share the App translation unit; `GuiActionHandler::Deps` is large, while action parity already exists.

**Boundary verification**
- [ ] Characterize epoch/preview/focus, local mutation, host creation, input route and command call order.
**Implementation and migration**
- [ ] Add safe tab/pane lookup operations. Extract one app-private reconciler/effect collaborator at a time with typed host/layout/frame ports; keep `TopologyProjection` in client.
- [ ] Group GUI command bindings by responsibility while retaining `execute()` and its canonical registry.
**Unit tests**
- [ ] Exercise projection retries, epoch reset, failed application, mutation ordering and command effects through fake ports; retain App smoke.
**Cross-platform validation**
- [ ] Check macOS menu dispatch and Windows actions, core aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document app-private ownership and update affected target sources.
**Acceptance criteria**
- [ ] App keeps orchestration order; extracted policy is testable without a live window or renderer.
