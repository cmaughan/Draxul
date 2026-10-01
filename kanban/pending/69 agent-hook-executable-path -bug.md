# Use the supplied executable location in agent hooks

**Summary:** Use the supplied application location so agent conversations can be reported outside the command search path.

**Priority:** 75  
**Severity:** HIGH  
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`

**Evidence and trigger:** B12; generated hooks invoke bare `draxul` despite receiving its executable location from the server.

- [ ] **Investigate:** Trace executable environment propagation and command construction for both agent integrations.
- [ ] **Fix:** Prefer the supplied location and pass paths safely, including spaces and non-English characters.
- [ ] **Acceptance:** Both integrations report native conversation references when the application is absent from the command search path.
- [ ] **Validation:** Check Windows and macOS hook behavior; run core aggregate tests and same-cache smoke.
