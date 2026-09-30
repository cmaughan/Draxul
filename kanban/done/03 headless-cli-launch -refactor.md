# Extract headless CLI and launch policy

**Priority:** P1 — CLI parsing and launch decisions currently inherit the graphics app.
**Source:** `app/CMakeLists.txt`
**Proposed by:** Claude 4, 5, 28. **Owner:** one app/CLI agent. **Prerequisite completed:** `00 test-graph-and-scope -refactor.md`.
**Original evidence:** CLI files compile into `draxul-app`; `main.cpp` mixes server/bootstrap decisions with native entry points; `ParsedArgs` layout depends on a render-test define.

**Boundary verification**
- [x] Record CLI grammar, environment precedence, helper paths and Windows/macOS launch arguments.
**Implementation and migration**
- [x] Add headless `draxul-app-cli` for parsers, dispatch, session resolution and injected IO/request ports. Make `ParsedArgs` fields unconditional.
- [x] Move pure bootstrap/status/argument decisions incrementally; keep console, detached helper execution, status surface and `AppOptions` mapping in app/platform code. Put executable layout in the client.
**Behavioral coverage**
- [x] Preserve existing parser coverage in `draxul-test-cli`; add command-dispatch/server integration for output, capability, path and quoted/unicode arguments. Use isolated cases only for platform policy that cannot be exercised locally.
**Cross-platform validation**
- [x] Verify macOS helper/bundle launch, core aggregate and same-cache startup smoke; preserve Windows helper/quoting behavior and CI coverage. Routine Windows CI is not a separate pending gate.
**Agent documentation and tooling**
- [x] Add CLI target and headless closure to module map and dependency checks.
**Acceptance criteria**
- [x] CLI tests build without renderer/window; existing CLI behavior and native launch remain intact.

**Execution plan and boundaries**
- One vertical slice: extract existing launch/subcommand parsers and dispatch, injectable output/input/control-request ports, session resolution and pure startup policy into `libs/draxul-app-cli`; keep native entry points, console attachment, process launch, status surfaces and `AppOptions` mapping in app/platform code.
- Move executable-layout policy into the existing headless client library. Preserve CLI grammar and subcommand dispatch precedence, exit codes and environment precedence. `ParsedArgs` has one unconditional layout; render-flag recognition remains build-capability gated.
- Prefer real server/control integration and smoke; preserve existing cases. The narrow test target must not link or build the app/window/renderer. Complete with one core aggregate and same-cache startup smoke; visual/product behavior is unchanged.
- Existing uncommitted test-ownership work is the starting point. Concurrent Kanban labels/priority edits belong to another task and are preserved.

**Behavior preserved**
- Dispatch order: integration, topology, UI/agent control, then launch flags. Topology parse errors exit 2; other parse errors exit 1. Parse/console planning runs before output on Windows.
- Topology uses explicit Session/runtime options ahead of `DRAXUL_SESSION_ID`/`DRAXUL_SERVER_RUNTIME_DIR`; current pane/Space/tab IDs and control routes still use the existing injected environment. Integration home resolution still prefers `CODEX_HOME`/`CLAUDE_CONFIG_DIR` over platform home.
- Windows Unicode argv conversion, quoting, helper-copy/launch and redirected-console handling remain intact. Existing macOS nested helper paths and helper-to-client mapping move verbatim into the client; `execv`, Cocoa activation and status surfaces stay native.
- CLI runners have per-call `CliContext` streams and a control-request port. Topology/terminal clients retain their real protocol behavior; a real isolated server covers those paths without a GUI. Existing parser and status/path cases are moved rather than duplicated or replaced.
- Initial headless target: 38 existing cases / 187 assertions passed in 0.02 s. After new dispatch/server coverage: 41 cases / 264 assertions passed in 0.16 s. These focused runs were development checks before the final aggregate.

**Completed result and validation**
- Extracted launch, topology/control/integration CLI and session-ID files into `libs/draxul-app-cli/`, with public `<draxul/...>` headers. Removed the old app-local copies. `main.cpp` now delegates command dispatch, shared-server capability/error decisions and session naming. Pure status formatting lives with the CLI; client-owned executable-layout policy has no native UI dependency.
- `draxul-app-cli` and `draxul-test-cli` have configure-time renderer/window bans; the test target additionally bans `draxul-app`. The focused build log contains no app, renderer, window or executable build. Real server integration uses temporary runtime state and cleans up its server.
- Configure/generate: 12.4 s + 1.3 s for the explicit reconfigure that registered the new target; passed. Compilation: the headless target and later core aggregate/app rebuild passed; compilation elapsed time was not recorded independently. The initial attempt to build a newly introduced Make target required that explicit reconfigure. A later aggregate attempt was rejected while another task owned the shared cache, then succeeded after release.
- Focused development: 38 original cases in 0.02 s, then 41 cases including new dispatch/server coverage in 0.16 s; both passed. Existing status/path tests moved afterwards and were covered by the aggregate rather than another focused run.
- Final core aggregate: `python3 do.py test debug`, 55/55 CTest entries passed in 39.74 s. The CLI entry passed in 1.30 s; existing native helper/process, server and app integration entries passed.
- Same-cache startup: `python3 do.py smoke --skip-build`, passed in 1.88 s; Metal initialized successfully. Cache remained Debug for this refactor. No extra Release build: this preserves behavior and introduces no user-facing feature or bug fix.
- Product suites, render comparisons and external SDK rebuild were skipped: no product, rendering or SDK behavior changed. Windows runtime execution/remote CI were not performed locally; Unicode argv conversion, detached helper/quoting logic and platform-gated tests remain in place for normal CI.
- Useful review boundary: `cli_dispatch` preserves grammar precedence and exit codes while planning Windows console setup before IO; `launch_policy` preserves capability order/session naming; `executable_layout` carries the existing platform mappings. No additional refactor card was started.
