# Consolidate SatView’s CSV tokenizer

**Priority:** P2 — one lexical implementation prevents divergence across four catalog entry points.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 7.  
**Owner:** One SatView core/catalog agent.  
**Dependencies:** None among accepted refactors.

**Evidence:** `plugins/satview/src/core/satview_catalog.cpp:715–803` and `src/core/satview_surface_catalog.cpp:20–112` duplicate quoted-field/line-ending state machines. Consumers appear at catalog `:1174,1293,1415` and surface `:249`. Both sources already belong to `draxul-satview-core STATIC`. Existing SATCAT quote/CRLF coverage is at `tests/satview_catalog_tests.cpp:170–198`.

**Boundary:** One private tokenizer within existing `draxul-satview-core`, using standard-library rows and typed lexical errors. Callers retain diagnostics, headers, numeric parsing, filtering, and record validation. No new production library or public API. The completed binary-container refactor does not cover this text tokenizer.

#### Boundary verification

- [x] Compare both implementations’ quotes, escaped quotes, embedded newlines, CRLF/lone CR, final fields, empty rows, and error output.
- [x] Record intentionally different generic versus surface error messages.
- [x] Preserve accumulated rows on failure and each caller’s handling of them.

#### Implementation and migration

- [x] Characterize current lexical behavior before moving it.
- [x] Extract the common implementation and migrate the generic catalog callers first.
- [x] Migrate surface parsing with explicit translation back to its current diagnostics.
- [x] Remove duplicate code after all four entry points delegate.
- [x] Keep numeric-validation and domain-record policies in their existing owners.

#### Unit tests

- [x] Add one lexical matrix covering empty input, delimiters, escaped/embedded quotes, embedded newlines, CRLF/lone CR, trailing blank rows, malformed quotes, and partial output.
- [x] Retain caller-level checks for all four entry points and exact diagnostic mapping.
- [x] Give tests explicit private-header access within the existing product suite.
- [x] Build `cmake --build <cache> --config Debug --target draxul-satview-core draxul-test-satview --parallel`.
- [x] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-satview-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Preserve byte/line-ending behavior on Windows/macOS and unchanged records supplied to Vulkan/Metal.
- [x] Keep the helper free of SDL/GPU dependencies.
- [x] Run `python3 do.py test debug --satview`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [x] Document tokenizer ownership separately from catalog semantics and binary framing.
- [x] Keep tests in the existing product classification and record a focused Catch tag.
- [x] Commit in SatView and adopt its pointer deliberately. The extraction landed in
      SatView commit `8468698`; the root now records descendant pointer `034d757`.

#### Acceptance criteria

- [x] All four consumers use one lexical implementation.
- [x] Diagnostics, partial output, empty-row behavior, and domain validation remain compatible.
- [x] Lexical cases require no graphics initialization; no graphics-free claim is made for the existing broad product test build.
- [x] Each migration step builds independently. The callers were migrated in one
      buildable product checkpoint, followed by focused SatView and aggregate gates.
