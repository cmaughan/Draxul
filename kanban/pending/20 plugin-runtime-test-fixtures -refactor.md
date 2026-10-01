# Give product runtime tests contract-level fixtures

**Summary:** Give plugin tests lightweight stand-ins for the services they actually use so ordinary behaviour tests do not need the full host and graphics system.

**Priority:** P2 — product unit tests inherit core host/renderer fakes.  
**Source:** `tests/support/fake_renderer.h`  
**Proposed by:** Claude 44. **Owner:** one shared test-support agent, then product test owners.  
**Evidence:** MegaCity/SatView fixtures include core fake window/renderer and link host/renderer despite runtime-level subjects; plugin dependency audit checks production targets only.

**Boundary verification**
- [ ] Separate legitimate host-loading integration cases from runtime-contract unit cases.
**Implementation and migration**
- [ ] Add contract-only frame/callback fakes using existing runtime callbacks; migrate suitable product tests and add a narrow test-target allowlist.
**Unit tests**
- [ ] Compile runtime tests from declared plugin interfaces; preserve host integration fixtures.
**Cross-platform validation**
- [ ] Check Vulkan/Metal header closure, product aggregates and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document fixture selection and allowed test dependencies.
**Acceptance criteria**
- [ ] Runtime unit targets avoid core renderer/host links unless their cases exercise those integrations.
