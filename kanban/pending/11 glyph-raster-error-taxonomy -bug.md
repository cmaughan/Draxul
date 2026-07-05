# Correct glyph rasterization error taxonomy

**Type:** bug
**Priority:** 11
**Raised by:** Claude

## Problem

`GlyphCache::rasterize_cluster()` reports `FT_Load_Glyph`, `FT_Render_Glyph`, and bitmap conversion failures as `ErrorKind::AtlasOverflow`. Callers can then run atlas-reset/retry policy for a font/raster error.

## Implementation plan

- [ ] Add or reuse distinct error kinds for font load, glyph rasterization, invalid bitmap, and true atlas capacity.
- [ ] Map each FreeType/conversion failure to the correct kind and include face/style/glyph context without logging unbounded user text.
- [ ] Audit all callers that branch on `AtlasOverflow`; retry/reset only for actual capacity failures.
- [ ] Preserve fallback-font attempts where another face may legitimately render the cluster.
- [ ] Surface a rate-limited user warning only after fallback is exhausted; do not duplicate the iceboxed font inspector.

## Tests

- [ ] Inject load, render, conversion, and reserve failures independently.
- [ ] Prove only reserve failure triggers atlas reset/retry.
- [ ] Prove a failing face can fall through to a valid fallback without poisoning the cache.

## Acceptance criteria

- [ ] `AtlasOverflow` means capacity exhaustion only.
- [ ] Font failures do not clear a healthy atlas.
- [ ] Font tests, `ctest`, render tests, and smoke pass.

## Dependencies and parallelism

Small font-focused item; coordinate with active shared atlas work if it changes error plumbing.

<model>GPT-5 Codex</model>
