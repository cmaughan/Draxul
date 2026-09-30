# Export explicit installed C++ plugin support targets

**Priority:** P2 — standalone consumers reconstruct installed link edges.  
**Source:** `sdk/CMakeLists.txt`  
**Proposed by:** Claude 42; product evidence from Claude 48. **Owner:** one SDK agent, then ScoreView owner.  
**Evidence:** SDK exports `PluginSDK` only while types/performance/helper headers are installed elsewhere; ScoreView manually imports them and hand-rolls path services.

**Boundary verification**
- [ ] Inventory shipped C++ headers, transitive GLM/JSON dependencies and current ScoreView extraction requirements.
**Implementation and migration**
- [ ] Keep pure C ABI target; export optional current-version C++ support targets and bounded service helpers, then migrate standalone ScoreView.
**Unit tests**
- [ ] Extend existing external SDK smoke to compile/link every shipped C++ surface used by products.
**Cross-platform validation**
- [ ] Test Windows and macOS installed package/extraction plus aggregate and smoke.
**Agent documentation and tooling**
- [ ] Document C versus C++ SDK ownership and installed targets.
**Acceptance criteria**
- [ ] Standalone product no longer reconstructs private archive paths.
