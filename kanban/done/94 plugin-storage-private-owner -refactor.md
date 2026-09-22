# Give PluginHost storage a private owner

**Priority:** P2 — separates filesystem policy from native rendering and reload lifecycle while enabling deterministic failure tests.
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 5.
**Owner:** One core host/storage agent.
**Dependencies:** Coordinated with `kanban/done/58 plugin-storage-error-preservation -bug.md`; no dependency on another newly accepted refactor. Coordinated source ownership with `kanban/done/02 host-layer-static-libraries -refactor.md`.

**Evidence:** `libs/draxul-host/src/plugin_host.cpp:63–85,156–203,489–519,734–778,898–1002` owns paths, validation, buffering, replacement, and overlays. State resides in `plugin_host.h:157–172`. Tests access it through loaded modules and initialized hosts (`tests/plugin_manager_tests.cpp:350–389,898–935`).

**Boundary:** Private `PluginStorage` sources inside existing `draxul-host`, with path/scope queries, JSON read/write/remove, and overlay begin/commit/discard operations. PluginHost retains ABI callbacks, pointer/thread/generation checks, and reload orchestration. Filesystem/JSON details stay private; no SDK or PluginSupport expansion and no new production library.

#### Boundary verification

- [x] Pin path layout, scope/key validation, result precedence, JSON limits, and NUL-inclusive buffer sizing.
- [x] Pin staged writes/removals, disk fallback, discard, and best-effort partial commit.
- [x] Preserve App’s cohort use of `PluginHost::finalize_reload_storage()`; storage must not own activation policy.

#### Implementation and migration

- [x] Mechanically move storage state, path calculation, and operations into the collaborator.
- [x] Delegate ABI operations after existing host checks.
- [x] Introduce narrow private filesystem-operation injection for failure tests.
- [x] Preserve or adopt the fix from the existing storage-error card without duplicating its ownership.
- [x] Keep each migration buildable and preserve reload sequencing.

#### Unit tests

- [x] Test scoped paths, invalid/unavailable scopes, JSON validity/limits, missing files, and two-call buffer behavior directly.
- [x] Test overlay visibility, tombstones, discard, successful commit, failure, and current partial publication.
- [x] Reuse the existing bug card’s injected open/write/flush/replace/cleanup assertions.
- [x] Retain real-module, stale-callback, wrong-thread, and rollback integration coverage.
- [x] Build `draxul-plugin-host` and `draxul-test-app` from the Debug cache.
- [x] Run the four direct `[plugin][storage]` cases and the aggregate app shards.

#### Cross-platform validation

- [x] Preserve Windows `MoveFileExW` flags, POSIX rename, UTF-8 paths, and platform storage roots.
- [x] Preserve reload/GPU lifetime ordering on Vulkan and Metal.
- [x] Run `python3 do.py test debug --products`, then `python3 do.py smoke --skip-build`.

#### Agent documentation/tooling

- [x] Document private storage versus host lifecycle ownership and partial-commit semantics.
- [x] Provide explicit test-only access without exporting implementation headers.
- [x] Document that direct cases avoid module initialization, while the existing app test target retains its build closure.

#### Acceptance criteria

- [x] Storage policy is directly testable without loading a plugin.
- [x] ABI status codes, paths, limits, callbacks, and reload outcomes remain compatible.
- [x] Existing overlay behavior is preserved; no multi-file transaction rewrite is introduced.
- [x] Each implementation responsibility has one owner and native integration tests remain intact.

#### Completion evidence

- Direct storage suite: 4 cases and 99 assertions passed.
- Aggregate: `python3 do.py test debug --products` passed all 39 CTest shards in 113.73 seconds.
- Same-cache smoke: `python3 do.py smoke --skip-build` passed with the Metal renderer initialized.
- Windows replacement flags and the POSIX rename path were preserved by source review; the aggregate and smoke ran on macOS.
