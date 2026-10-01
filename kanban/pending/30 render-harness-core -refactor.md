# Separate CPU render harness from capture driver and demo host

**Summary:** Separate visual-test setup and image comparison from the live drawing system so those parts can be built and tested without starting graphics code.

**Priority:** P2 — scenario/diff work inherits renderer, font, host and NanoVG links.  
**Source:** `libs/draxul-render-test/CMakeLists.txt`  
**Proposed by:** Claude 38. **Owner:** one render-harness agent. **Coordinates with:** card 13.  
**Evidence:** one target compiles parser/diff, capture driver and `NanoVGDemoHost`; `render_test.h` includes concrete renderer and `AppOptions`.

**Boundary verification**
- [ ] Classify parser/diff/path/export, AppOptions mapping, capture and demo host dependencies.
**Implementation and migration**
- [ ] Add CPU `draxul-render-harness-core`; retain native capture driver and host-side demo target. Move scenario-to-AppOptions mapping to app/driver.
**Unit tests**
- [ ] Add narrow harness parser/diff tests and link-closure rejection; keep real render tests.
**Cross-platform validation**
- [ ] Check Windows/macOS render scenarios, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Update module map and narrow target command.
**Acceptance criteria**
- [ ] Harness core builds without renderer, host, font or NanoVG implementation.
