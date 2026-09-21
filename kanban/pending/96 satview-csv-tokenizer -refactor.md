# Consolidate SatView’s CSV tokenizer

**Priority:** P2 — one lexical implementation prevents divergence across four catalog entry points.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 7.  
**Owner:** One SatView core/catalog agent.  
**Dependencies:** None among accepted refactors.

**Evidence:** `plugins/satview/src/core/satview_catalog.cpp:715–803` and `src/core/satview_surface_catalog.cpp:20–112` duplicate quoted-field/line-ending state machines. Consumers appear at catalog `:1174,1293,1415` and surface `:249`. Both sources already belong to `draxul-satview-core STATIC`. Existing SATCAT quote/CRLF coverage is at `tests/satview_catalog_tests.cpp:170–198`.

**Boundary:** One private tokenizer within existing `draxul-satview-core`, using standard-library rows and typed lexical errors. Callers retain diagnostics, headers, numeric parsing, filtering, and record validation. No new production library or public API. The completed binary-container refactor does not cover this text tokenizer.

#### Boundary verification

- [ ] Compare both implementations’ quotes, escaped quotes, embedded newlines, CRLF/lone CR, final fields, empty rows, and error output.
- [ ] Record intentionally different generic versus surface error messages.
- [ ] Preserve accumulated rows on failure and each caller’s handling of them.

#### Implementation and migration

- [ ] Characterize current lexical behavior before moving it.
- [ ] Extract the common implementation and migrate the generic catalog callers first.
- [ ] Migrate surface parsing with explicit translation back to its current diagnostics.
- [ ] Remove duplicate code after all four entry points delegate.
- [ ] Keep numeric-validation and domain-record policies in their existing owners.

#### Unit tests

- [ ] Add one lexical matrix covering empty input, delimiters, escaped/embedded quotes, embedded newlines, CRLF/lone CR, trailing blank rows, malformed quotes, and partial output.
- [ ] Retain caller-level checks for all four entry points and exact diagnostic mapping.
- [ ] Give tests explicit private-header access within the existing product suite.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-satview-core draxul-test-satview --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-satview-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve byte/line-ending behavior on Windows/macOS and unchanged records supplied to Vulkan/Metal.
- [ ] Keep the helper free of SDL/GPU dependencies.
- [ ] Run `python3 do.py test debug --satview`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Document tokenizer ownership separately from catalog semantics and binary framing.
- [ ] Keep tests in the existing product classification and record a focused Catch tag.
- [ ] Commit in SatView and adopt its pointer deliberately.

#### Acceptance criteria

- [ ] All four consumers use one lexical implementation.
- [ ] Diagnostics, partial output, empty-row behavior, and domain validation remain compatible.
- [ ] Lexical cases require no graphics initialization; no graphics-free claim is made for the existing broad product test build.
- [ ] Each migration step builds independently.
