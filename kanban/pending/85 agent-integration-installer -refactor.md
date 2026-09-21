# Separate integration installation from CLI presentation

**Priority:** P2 — isolates configuration-preservation logic from the app and removes environment mutation from installer tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 6.  
**Owner:** One installer agent; coordinate app/test CMake centrally.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` and `kanban/pending/01 app-local-cmake-ownership -refactor.md`. No dependency on another accepted proposal.

**Evidence:** `app/agent_integration.cpp` owns environment paths at `:168–196`, persistence at `:198–228`, provider transformations at `:246–426`, status inspection at `:429–585`, and CLI execution/output at `:636–811`. It belongs to broad `draxul-app`; installer cases in `tests/control_plane_tests.cpp:165–302` alter process environment.

**Boundary:** Add `draxul-agent-integration STATIC` with explicit configuration paths, provider/action requests, typed status, and typed results. Keep JSON, scripts, transformations, and persistence private. App retains argument parsing, environment resolution, text/JSON presentation, and exit codes. Dependencies are STL/filesystem and private JSON.

#### Boundary verification

- [ ] Inventory output schemas, hook markers/version, paths, provider differences, and error behavior.
- [ ] Coordinate or land `kanban/pending/18 codex-indented-features-header -bug.md` before moving its parser; preserve its regression.
- [ ] Confirm the completed review-script deduplication card does not overlap installed session hooks.

#### Implementation and migration

- [ ] Extract provider-specific document transformations as private pure functions.
- [ ] Add explicit-path inspect/install/uninstall operations and typed results.
- [ ] Make CLI handling delegate while preserving output and exit codes.
- [ ] Keep Claude matcher/path semantics and Codex features mutation distinct.
- [ ] Move installer cases to focused tests; retain CLI integration coverage.

#### Unit tests

- [ ] Add `draxul-test-agent-integration` for idempotency, unrelated-content preservation, malformed documents, status/version inspection, owned-hook removal, and filesystem failures.
- [ ] Use explicit temporary directories without process-environment mutation.
- [ ] Add a public-header/link-isolation consumer that does not inherit app or JSON API dependencies.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-test-agent-integration --parallel`.
- [ ] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-agent-integration-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve PowerShell quoting, POSIX executable permissions, hook payloads, server epoch, and runtime-generation routing.
- [ ] Exercise platform filesystem replacement/error behavior.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`, with Windows equivalents.
- [ ] Verify no graphics or host dependency enters the installer target.

#### Agent documentation/tooling

- [ ] Register focused/isolation targets in aggregates and `do.py` selection tests.
- [ ] Update the module map and add concise installer ownership guidance.
- [ ] Keep user-visible integration documentation accurate without claiming new capabilities.

#### Acceptance criteria

- [ ] Installation/status operations are callable through explicit paths and typed results.
- [ ] CLI behavior and provider configuration preservation remain compatible.
- [ ] Focused installer tests build without the application target.
