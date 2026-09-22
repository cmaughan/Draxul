# Separate Tree-sitter source parsing from directory scanning

**Priority:** P2 — removes filesystem/thread fixtures from grammar cases while preserving immutable scan publication.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 6.  
**Owner:** One MegaCity parser/scanner agent.  
**Dependencies:** No newly accepted production-boundary prerequisite; `kanban/pending/00 internal-target-build-policy -refactor.md` applies to the proposed focused test target.

**Evidence:** `plugins/megacity/product/draxul-treesitter/src/treesitter.cpp:504–782` combines parser/query resources, traversal, reading, progress, parsing, and publication. Grammar tests start workers on temporary files (`plugins/megacity/tests/treesitter_tests.cpp:48–78`). Existing `draxul-treesitter STATIC` owns Tree-sitter core/grammar and support-type dependencies.

**Boundary:** A synchronous private parser/resource owner inside `draxul-treesitter`: source text plus normalized relative path → existing `ParsedFile` with explicit success/skip status. Keep native parser/query/tree handles private. Scanner retains reading, filtering, cancellation, progress, and publication; `SemanticSourceController` continues consuming unchanged snapshots.

#### Boundary verification

- [x] Capture record order, path normalization, query reuse/fallback, parse-error records, and progress semantics.
- [x] Preserve null-tree skip behavior and increment parsed count only after successful parsing (`treesitter.cpp:582–588`).
- [x] Keep downstream model/layout work in `plugins/megacity/kanban/pending/05 megacity-model-layout-routing-library -refactor.md`.

#### Implementation and migration

- [x] Extract RAII parser/query ownership with one reusable parser context per scan.
- [x] Move per-source extraction into the synchronous operation.
- [x] Delegate the worker’s parsing step without changing traversal or publication.
- [x] Move grammar-focused cases to in-memory fixtures.
- [x] Add proposed product-owned `draxul-test-megacity-parser`, linked only to the parser library and test support; retain scanner integration cases in their existing suite.

#### Unit tests

- [x] Cover functions/methods, forward declarations, fields/references, inheritance, abstract classes, includes, and malformed source.
- [x] Preserve optional-query fallback and successful-versus-skipped parse accounting.
- [x] Retain restart, path-filter, cancellation, completion, and semantic-consumer integration tests.
- [x] Keep deferred cap/mixed-file coverage in `plugins/megacity/kanban/ice-box/13 codebasescanner-parse-error-cap -test.md`.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-treesitter draxul-test-megacity-parser --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-megacity-parser-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Verify record/path behavior on macOS and preserve downstream City/Biology inputs on Metal.
- [ ] Compare record/path behavior on Windows and preserve downstream City/Biology inputs on Vulkan.
- [x] Keep the parser free of renderer/host dependencies and preserve MegaCity enabled/disabled build support.
- [x] Run `python3 do.py test debug --megacity`, then `python3 do.py smoke debug --skip-build`. (macOS: focused parser 4/4 cases and 52 assertions; core + MegaCity aggregate 26/26 CTest entries in 37.16s; same-cache Metal startup smoke passed.)

#### Agent documentation/tooling

- [x] Partition product test globs exactly once and add the focused target to product aggregates and `do.py` selection.
- [x] Update nested guidance with synchronous-parser versus scanner ownership and the selected Debug cache.
- [x] Commit implementation in MegaCity (`70feb80`) and adopt its pointer deliberately.

#### Acceptance criteria

- [x] Grammar tests require no temporary source files, worker polling, or graphics initialization.
- [x] Parser resources remain private and reused efficiently.
- [x] Scanner API, immutable snapshots, progress/completion semantics, and parsed records remain compatible.
- [x] The focused parser test build excludes host and renderer targets.
