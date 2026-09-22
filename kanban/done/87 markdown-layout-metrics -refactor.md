# Make Markdown layout consume font metrics

**Priority:** P2 — removes physical-font initialization from geometry tests with a small interface change.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 8.  
**Owner:** One Markdown layout agent.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` for the focused test target. Independent of other accepted proposals.

**Evidence:** `markdown_layout.h:80–84` requires `RichTextService&`, but `markdown_layout.cpp:259–350` uses it only for four metric lookups. `tests/markdown_layout_tests.cpp:22–36` loads a physical font. `MarkdownHost::rebuild_layout()` supplies the service at `markdown_host.cpp:392–402`.

**Boundary:** Within existing `draxul-markdown`, accept a metrics lookup from `RichTextStyleKey` to `FontMetrics`, or an equivalent immutable table. Host adapts the real font service. Preserve theme/style resolution; font loading, shaping, rasterization, and draw-list construction remain in their current owners. No new production library.

#### Boundary verification

- [x] Inventory all layout callers and metric uses, including body padding and base-style line height.
- [x] Define metric lifetime/value semantics and preserve existing pixel-scale behavior.
- [x] Confirm draw-list construction still requires the font library; do not promise target-wide font removal.

#### Implementation and migration

- [x] Add the metrics-based interface and adapt `MarkdownHost::rebuild_layout()`.
- [x] Convert layout internals without changing measurement arithmetic or style resolution.
- [x] Migrate geometry tests to synthetic metrics.
- [x] Remove the concrete-service overload after all callers move.

#### Unit tests

- [x] Cover narrow/wide/tall metrics, emphasis/base-style line heights, wrapping, tables, padding, and Unicode cell widths.
- [x] Retain representative real-font host/drawing integration coverage.
- [x] Add `draxul-test-markdown-layout` linked to `draxul-markdown`, excluding host/Kanban targets; avoid duplicate test registration.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-test-markdown-layout --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-markdown-layout-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Compare deterministic layout on Apple Clang.
- [x] Compare deterministic layout on MSVC.
- [x] Verify host metrics and drawing remain aligned across font-size/DPI changes.
- [x] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.
- [x] Run affected Markdown render coverage on Metal without changing rendering policy.
- [x] Run affected Markdown render coverage on Vulkan.

#### Agent documentation/tooling

- [x] Register the focused target in core aggregates and runner selection tests.
- [x] Document the metrics contract and the retained drawing/font dependency.

#### Acceptance criteria

- [x] Geometry tests execute without loading physical fonts.
- [x] Layout no longer accepts or owns a concrete font service.
- [x] Existing document geometry, style resolution, and drawing alignment remain compatible.

## Windows closeout (2026-09-22)

Deterministic MSVC layout coverage passed in the 48/48 products aggregate. The
new developer Markdown scenario exported a valid 1200x760 Vulkan frame, and
same-cache smoke passed.
