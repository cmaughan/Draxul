# Score host hangs under --screenshot-size and .musicxml + --screenshot

**Type:** bug
**Priority:** 74
**Raised by:** Claude (Opus 4.8), while verifying the analysis overlay 2026-07-17

## Symptom

Two headless-screenshot variants never write their file and the app must be killed,
both with `--host score --command "paged analysis unique"`:

- `--screenshot out.png --screenshot-size 900x1500` (any size tried; plain
  `--screenshot` without a size works every time) — the log reaches
  "score: engraved N page(s)" and then nothing.
- `--source grieg-waltz-op-12-no-2.musicxml --screenshot out.png` (the UNCOMPRESSED
  source) — identical command with the `.mxl` works. Suspect the slicer-ready path
  (windowed streaming available) differs from the `.mxl` monolith fallback in what
  `run_screenshot`'s readiness pump waits for.

`--gui-action font_decrease --screenshot` also hung (that path pumps
`run_smoke_test(10s)` first — possibly the same underlying wait).

## Why it matters

Headless screenshots are the verification loop for visual features (the analysis
overlay work relied on them); only the narrowest variant works, which blocks
capturing anything below the first-window fold.

## Where to look

- `app/main.cpp` ~756 (`--screenshot-size` -> `render_target_pixel_*` overrides) and
  ~915 (`run_screenshot`) — what does the readiness pump wait on, and does it differ
  between render-target and window capture?
- Score host paged mode with slicer-ready sources: does anything (async engraver,
  stream controller) hold the "ready" signal the pump needs?

## Acceptance

- [ ] `--screenshot-size WxH` writes a BMP at the requested size for the score host.
- [ ] `.musicxml` sources screenshot identically to `.mxl`.
- [ ] A regression note in the do.py smoke or a headless test covering one sized capture.
