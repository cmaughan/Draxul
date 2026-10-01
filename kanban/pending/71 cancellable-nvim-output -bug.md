# Make editor writes bounded and cancellable

**Summary:** Make large editor input cancellable so an editor that stops reading cannot freeze the interface.

**Priority:** 77  
**Severity:** HIGH  
**Source:** `libs/draxul-nvim/src/rpc.cpp`

**Evidence and trigger:** B14; synchronous message writes block the interface when a nonreading editor exhausts pipe capacity.

- [ ] **Investigate:** Trace request, notification, reply, and shutdown writes on both platforms.
- [ ] **Fix:** Introduce bounded outbound ownership and cancellable writes; cancel before shutdown waits.
- [ ] **Acceptance:** Oversized input to a nonreading child leaves the interface responsive, preserves message ordering, and permits bounded shutdown.
- [ ] **Validation:** Exercise both platform transports; run core aggregate tests and same-cache smoke.
