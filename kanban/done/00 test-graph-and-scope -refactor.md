# Make test targets and scope selection authoritative

**Priority:** P1 — broad test links slow and couple otherwise independent work.
**Source:** `tests/CMakeLists.txt`
**Proposed by:** Claude 1, 2, 18, 36; Codex 1, 13. **Owner:** one core test/CMake agent, with coordinated suite moves.
**Original evidence:** `draxul-test-core` links renderer, window, font, server and hosts and depends on `draxul`; `do.py` omits built app-shell/host-api and MegaCity model tests. Markdown parser cases and plugin tests sit in host/app-linked targets.

**Validation priorities (clarified with the user)**
- Preserve and prefer integration, vertical-slice, and smoke coverage of real behavior. Ownership describes dependencies and responsibility, not a preference for unit tests.
- Keep existing integration cases intact, including headless cases that exercise several libraries. Isolate executable-dependent cases without excluding them from the normal aggregate.
- Add unit tests only where isolation supplies meaningful additional evidence; do not add tests solely because a helper or target was extracted.
- Focused targets are iteration tools. Completion still requires the scope-appropriate aggregate and same-cache startup smoke; platform-specific manual gates are needed only for concrete risks not covered by existing CI.

**Boundary verification**
- [x] Inventory every test case, tag, platform gate, executable dependency, CTest label and product aggregate.
**Implementation and migration**
- [x] Split protocol, server/client, process, font and renderer suites by owning library; move only executable-dependent cases to integration. Separate Markdown core/host and plugin-loader/storage suites. Keep source-partition tripwires.
- [x] Let CMake register behavioral test scope and product aggregates; make `do.py` consume that inventory while preserving integration/render selection and `--label` intersection. Move keycode-only Nvim/Kanban links to `SDL3::Headers`.
**Coverage and validation**
- [x] Check case/label parity, zero-match handling, disabled products, and narrow target closure. Make prospective `do.py test debug --target draxul-test-server` and Markdown-core targets usable.
**Cross-platform validation**
- [x] Exercise Unix gated suites, product-inclusive aggregate and same-cache smoke locally; preserve ConPTY/Windows registration and DLL staging for routine cross-platform CI; keep render cases platform-filtered.
**Agent documentation and tooling**
- [x] Update `CLAUDE.md`, `do.py` tests and CI scope expectations.
**Acceptance criteria**
- [x] Preserve every existing integration case and its real dependencies; every existing case runs in its intended scope; a focused headless target does not build the app, GPU stack or staged products.

**Execution notes**
- 2026-09-30: Implement as one test-infrastructure slice: explicit source ownership and headless boundaries; CMake-owned scope inventory consumed by `do.py`; behavioral registration/selection checks; aggregate and startup smoke. No production behavior changes are intended.
- Source inventory: 243 classified sources, 2,391 static registrations and 404 tag tokens; 241 of the 242 original `*_tests.cpp` files are byte-for-byte unchanged; the plugin-loader source adds failure-status diagnostics to two existing assertions and extends its real-module integration case to protect borrowed manifest IDs across refresh (see `32 plugin-discovery-refresh-lifetime -bug.md`). Registrations/tags remain unchanged. Platform-conditional cases remain in their original sources.
- Narrow-build diagnosis found genuine server dependencies in topology projection and stream-load cases; both remain real headless integration coverage in the server suite. Shared test support now declares its logging dependency directly instead of borrowing it through unrelated libraries.
- CMake registers scope labels and aggregate dependencies together; app-shell/host-API and MegaCity-model coverage is no longer omitted by a second Python name list. Product script integration and manifest-selected renders keep their scope. Standalone external SDK smoke remains separately selectable (and is included in the complete integration inventory).
- MegaCity and ScoreView test-package hooks now target the plugin integration suite, preserving their original native module/DLL dependencies.
- Markdown navigation is now compiled into the existing Markdown core target (SDL headers only); its state-machine, parsing, scroll and draw-list suites no longer link the GPU host. No production source behavior changed; Markdown cases remain intact, including host-dependent parser cases in their existing host-linked suite.
- Seven headless targets (configuration, protocol, server, client, process, font, Markdown core) built successfully with all five optional products disabled. CTest registered only `scope-core`; configure-time transitive-link guards reject app/window/renderer dependencies. All products were then restored in the same Debug cache. Disable/build/restore check: 56.25 s.
- Real CMake/CTest integration checks exercise aggregate membership, core/product selection, integration labels, label intersection, zero matches and disabled products. Existing Python command tests retain zero-match failure checks. Broad CI still runs all registered CTest entries; the shell/batch `--unit` wrappers now include both unit and integration labels so reclassification cannot discard previous coverage.
- Moving native-loader tests exposed missing SDL symbols in the narrower macOS executable. It now supplies the full SDL static library; the existing MegaCity City/Biology and ScoreView public-ABI cases pass. Windows Verovio DLL staging moved with its consumer; ConPTY source/platform guards remain intact.
- The first complete-inventory run exposed two previously unselected integration failures: borrowed manifest lifetime in bundled-plugin discovery (tracked and fixed in `32 plugin-discovery-refresh-lifetime -bug.md`), and the standalone SDK fixture invoking a root-only CMake policy helper. The fixture now applies that helper only when built inside Draxul, using its existing standalone-mode condition; the installed SDK remains self-contained.

**Validation cost and iteration record**
- Focused development: 15 Python workflow/selection tests passed in 0.33 s. Narrow compilation was repeated while resolving actual dependency/link errors; those individual attempts were not timed separately. Three existing server/scrollback cases passed in isolated diagnosis (about 1.3 s total); two native product ABI cases passed after fixing SDL linkage (31 assertions; wall time not recorded separately). These cases are repeated by the aggregate intentionally because they diagnosed failures.
- Initial product-scoped aggregate: 79 CTest entries, 163.02 s, four failed entries (obsolete Python source-string assertion, two timing-sensitive server entries, missing SDL symbols in native-loader tests). Eight product renders passed in that run; their individual durations sum to 69.02 s and overlap the parallel CTest wall time. The shared cache was switched to Release by another launch during this attempt, so it is not the final Debug handoff evidence.
- First locked complete Debug pass: configure/generate 13.14 s, compilation 50.89 s (app plus complete test aggregate), startup smoke 1.48 s, 72 CTest entries in 191.47 s: 70 passed, bundled-plugin discovery and external-SDK configuration failed. Both uncovered real defects and warranted the corrective rerun.
- No new isolated unit cases were added. New coverage is real CMake/CTest selection integration; the existing native-loader integration also now exercises borrowed manifest IDs across load/reload.
- The next SDK attempt built its external module but exposed another stale fixture assumption: its flat plugin directory is no longer a discoverable package. Shared SDK staging now uses the current generation directory/`current.json` format (also benefiting the ScoreView extraction fixture), and the triangle's user-tier copy preserves that package. Failed CLI discovery now includes captured diagnostics. No legacy-layout fallback was added to production.
- SDK build/package fixture corrections are recorded separately in `33 external-sdk-smoke-packaging -bug.md`. The second locked Debug aggregate passed 71/72 entries (293.78 s); only the SDK fixture required a subsequent targeted rerun. Its app/test build took 29.39 s (including automatic CMake regeneration), and same-cache startup smoke took 1.68 s. Other passed cases are not repeated after test-fixture-only edits.
- Final targeted SDK rerun: 1/1 CTest entry passed in 231.09 s, including public SDK install, isolated standalone build, bundled and user-tier discovery, and a nonblank raw-GPU render. Combined with the preceding 71 passed entries, all 72 complete-inventory checks are verified. The isolated package-layout sanity check took under one second before that expensive rerun.
- Final core render snapshots were skipped because this slice does not change visual behavior; the earlier product-scoped run passed eight product scenarios, and the final SDK gate rendered its external plugin. Windows/ConPTY execution and remote CI were not run locally; corresponding sources, platform conditions and runtime DLL staging remain registered for usual CI.
- Final Release confirmation: `python3 do.py run release --console -- --smoke-test` passed (one app build/startup, 35.54 s total, including configure/generate and incremental compilation). The normal build cache is now Release. This is the explicit final configuration switch required for the accompanying plugin bug fix; Debug aggregate/smoke evidence above came from the same locked Debug cache.
