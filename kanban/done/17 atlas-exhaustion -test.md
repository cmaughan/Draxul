# Glyph-atlas exhaustion coverage

**Type:** test
**Disposition:** Covered by the reset-and-retry policy.

`tests/glyph_raster_error_tests.cpp` covers typed atlas overflow, overflow flags,
the single reset/retry path, and warning rate limits. Multi-host reset behavior is
covered by `tests/atlas_overflow_multi_host_tests.cpp`. Bounded multi-page growth,
which would avoid the disruptive reset, remains in
`kanban/ice-box/27 atlas-dynamic-growth -feature.md`.
