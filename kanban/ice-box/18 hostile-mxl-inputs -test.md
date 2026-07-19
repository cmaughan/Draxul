# Hostile compressed MusicXML inputs

**Type:** test
**Priority:** 18
**Raised by:** Claude; Gemini requested corrupted notation coverage

## Gap

ScoreView tests a valid `.mxl`, generic garbage, and rejection by the pure XML importer, but the Verovio ZIP-loading path has no bounded negative corpus for truncated, malformed, encrypted, overlapping, or decompression-bomb archives.

## Implementation plan

- [ ] Define maximum compressed bytes, uncompressed bytes, entry count, per-entry size, nesting/path rules, and load/layout deadline for `.mxl` input.
- [ ] Add a lightweight preflight around the archive path before passing data to Verovio, or configure the embedded ZIP loader with equivalent hard limits if its API supports them.
- [ ] Require the standard container/root score relationship; reject traversal paths, duplicate ambiguous roots, encrypted entries, invalid central-directory offsets, and unsupported compression cleanly.
- [ ] Return user-facing errors through `ILayoutEngine::load` without throwing across host boundaries.
- [ ] Keep ordinary MusicXML behavior and valid compressed Grieg loading unchanged.

## Tests and acceptance

- [ ] Check in or generate tiny deterministic fixtures for truncated local header, missing/wrong central directory, CRC mismatch, duplicate root, traversal name, encrypted flag, excessive entry count, huge declared expansion, and random bytes.
- [ ] Assert every case fails within a strict time/memory budget with a non-empty stable error category.
- [ ] Run the corpus repeatedly under ASan/UBSan and add a seeded archive-structure fuzz target if the preflight parser is project-owned.
- [ ] Valid `.mxl` files continue to load and reflow.

## Dependencies and parallelism

Independent test/security task in the ScoreView core. Complete before type-aware drops and the piece library make `.mxl` opening more discoverable.

<model>GPT-5 Codex</model>
