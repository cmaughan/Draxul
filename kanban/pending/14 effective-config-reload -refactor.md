# Isolate checked config loading and reload differences

**Summary:** Share settings-loading checks and separate the logic that detects changes so reloads are easier to test while preserving working settings when a reload fails.

**Priority:** P2 — startup/reload duplicate loading while change classification lives inside App.  
**Source:** `app/app.cpp`  
**Proposed by:** Claude 29, narrowed. **Owner:** one app/config agent; serialize with card 04.  
**Evidence:** App repeats document/config loading and classifies changes inline; startup fallback and transactional reload intentionally differ.

**Boundary verification**
- [ ] Characterize fallback, override, staged font and all-or-old reload rules.
**Implementation and migration**
- [ ] Extract checked effective-config bundle and pure delta classifier; retain App publication and side effects.
**Unit tests**
- [ ] Test bundle errors and delta values; retain App reload failure/atlas lifetime integration.
**Cross-platform validation**
- [ ] Test font/config paths on Windows/macOS, aggregate and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document the loader versus App transaction responsibility.
**Acceptance criteria**
- [ ] Failed reload leaves previous config and font state intact.
