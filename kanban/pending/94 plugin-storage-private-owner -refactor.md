# Give PluginHost storage a private owner

**Priority:** P2 — separates filesystem policy from native rendering and reload lifecycle while enabling deterministic failure tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 5.  
**Owner:** One core host/storage agent.  
**Dependencies:** Coordinate `kanban/pending/58 plugin-storage-error-preservation -bug.md`; no dependency on another newly accepted refactor. Coordinate source ownership with `kanban/pending/02 host-layer-static-libraries -refactor.md`.

**Evidence:** `libs/draxul-host/src/plugin_host.cpp:63–85,156–203,489–519,734–778,898–1002` owns paths, validation, buffering, replacement, and overlays. State resides in `plugin_host.h:157–172`. Tests access it through loaded modules and initialized hosts (`tests/plugin_manager_tests.cpp:350–389,898–935`).

**Boundary:** Private `PluginStorage` sources inside existing `draxul-host`, with path/scope queries, JSON read/write/remove, and overlay begin/commit/discard operations. PluginHost retains ABI callbacks, pointer/thread/generation checks, and reload orchestration. Filesystem/JSON details stay private; no SDK or PluginSupport expansion and no new production library.

#### Boundary verification

- [ ] Pin path layout, scope/key validation, result precedence, JSON limits, and NUL-inclusive buffer sizing.
- [ ] Pin staged writes/removals, disk fallback, discard, and best-effort partial commit.
- [ ] Preserve App’s cohort use of `PluginHost::finalize_reload_storage()`; storage must not own activation policy.

#### Implementation and migration

- [ ] Mechanically move storage state, path calculation, and operations into the collaborator.
- [ ] Delegate ABI operations after existing host checks.
- [ ] Introduce narrow private filesystem-operation injection for failure tests.
- [ ] Preserve or adopt the fix from the existing storage-error card without duplicating its ownership.
- [ ] Keep each migration buildable and preserve reload sequencing.

#### Unit tests

- [ ] Test scoped paths, invalid/unavailable scopes, JSON validity/limits, missing files, and two-call buffer behavior directly.
- [ ] Test overlay visibility, tombstones, discard, successful commit, failure, and current partial publication.
- [ ] Reuse the existing bug card’s injected open/write/flush/replace/cleanup assertions.
- [ ] Retain real-module, stale-callback, wrong-thread, and rollback integration coverage.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-host draxul-test-app --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-app-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve Windows `MoveFileExW` flags, POSIX rename, UTF-8 paths, and platform storage roots.
- [ ] Preserve reload/GPU lifetime ordering on Vulkan and Metal.
- [ ] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Document private storage versus host lifecycle ownership and partial-commit semantics.
- [ ] Provide explicit test-only access without exporting implementation headers.
- [ ] Document that direct cases avoid module initialization, while the existing app test target retains its build closure.

#### Acceptance criteria

- [ ] Storage policy is directly testable without loading a plugin.
- [ ] ABI status codes, paths, limits, callbacks, and reload outcomes remain compatible.
- [ ] Existing overlay behavior is preserved; no multi-file transaction rewrite is introduced.
- [ ] Each implementation responsibility has one owner and native integration tests remain intact.
