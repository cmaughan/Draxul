# Complete the App control-request boundary

**Priority:** P1 — isolates frequently changed control contracts from App lifecycle and expensive fixtures.
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 2.
**Owner:** One App/control agent; serialize edits to `app/app.cpp`.
**Dependencies:** No new-refactor prerequisite. Coordinate `kanban/pending/01 app-local-cmake-ownership -refactor.md` and `kanban/pending/85 agent-integration-installer -refactor.md` for source/test inventory edits.

**Evidence:** `app/app.cpp:5426–5918` mixes decoding, validation, native-session reporting, wait/input policy, mutations, and response construction. `app/control_request_router.h:13–26` already supplies a read boundary. Both belong to `draxul-app` in root `CMakeLists.txt:329–390`. Mutation tests require initialized App, fonts, fake graphics, asynchronous requests, and pumping (`tests/control_plane_tests.cpp:36–54,344–373`).

**Boundary:** Extend app-private routing with typed operation callbacks and value results. Router owns request policy and response mapping; App retains subsystem ownership and ordered focus/layout/input/persistence/frame effects. Preserve the existing read path. No new production library, public API, or renderer/window/transport dependency in callback contracts.

#### Boundary verification

- [x] Inventory each request family, existing limits, errors, response shapes, and observable effect order.
- [x] Keep local versus server routing and authoritative Session/projection responsibilities separate.
- [x] Characterize `agent.start` activating an optional Space before validating later arguments (`app.cpp:5700–5727`); preserve that order during extraction.

#### Implementation and migration

- [x] Define narrow callbacks for lookup/readback, focus, launch/restart, input, native-session acceptance, reload, and event reads.
- [x] Extract wait and input policy first, then migrate remaining families individually.
- [x] Retain App adapters for lifecycle and ordered UI effects; do not pass `App&` into policy.
- [x] Preserve controller query implementations initially rather than expanding into their rewrite.
- [x] Keep Session-navigation work in `kanban/ice-box/22 app-tab-session-controllers -refactor.md`.

#### Unit tests

- [x] Add direct fake-operation tests for malformed requests, argument/text/key limits, rejected operations, input encoding, and response mapping.
- [x] Cover wait generation replacement, default predicates/status precedence, and stale/duplicate native-session routes.
- [x] Assert observable callback order and retain real endpoint/main-thread integration tests.
- [x] Build `cmake --build <cache> --config Debug --target draxul-app draxul-test-app --parallel`.
- [x] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-app-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Preserve Windows/macOS key-byte behavior and native endpoint dispatch.
- [x] Keep Vulkan/Metal effects behind App adapters; verify startup and focus behavior.
- [x] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [x] Document request-policy versus effect ownership in the module map.
- [x] Keep new tests explicitly classified and coordinate installer test moves.
- [x] Document direct-test runtime isolation without claiming the existing app test build is graphics-free.

#### Acceptance criteria

- [x] Request-policy cases execute without App initialization, physical fonts, or transport pumping.
- [x] App retains lifecycle authority and main-thread effects.
- [x] Existing error codes, JSON responses, effect ordering, and public control behavior remain compatible.
- [x] Every request-family migration remains independently buildable and reviewable.

#### Validation evidence

- Configure/generate after adding the policy and direct test inventory: 14.7 s + 1.1 s, passed.
- Focused `draxul-app` and `draxul-test-app` build: passed. The first sandboxed attempt stopped only because configured ccache could not write its external temp directory; the permitted rerun passed.
- Direct policy coverage: 47 assertions in 5 cases, passed without App initialization.
- App integration shards: 2/2 passed in 11.95 s. The first sandboxed run could not bind local authenticated control sockets; the permitted rerun passed unchanged.
- Core/App aggregate: 22/22 passed in 40.26 s.
- Same-cache Metal smoke: passed; renderer initialized on Apple M5 and the bounded smoke exited successfully.
