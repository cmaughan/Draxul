# Stop Kanban navigation when the host loses focus
**Severity:** HIGH  
**Source:** Codex #13; `modules/kanban/draxul-kanban/src/kanban_host.cpp:300`.

Holding navigation while switching tabs leaves repeat state active because the release reaches another host; background pumping continues selection changes.

**Investigation**

- [ ] Trace held-key/navigation state, focus transitions, background pumping, and pinned preview callbacks.

**Fix strategy**

- [ ] Clear repeat and navigation state on focus loss and guard unfocused repeat execution.

**Acceptance criteria**

- [ ] Hold, switch tabs, and release produces no subsequent background selection or preview changes.
- [ ] Refocusing requires fresh input and normal repeat still works; run core aggregate tests and same-cache smoke.
