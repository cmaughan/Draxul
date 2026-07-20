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

## Data point (2026-07-17)

A `.musicxml` + `--screenshot` run with `--command "paged analysis"` (no `unique`
token) SUCCEEDED — the earlier hanging `.musicxml` runs all carried
`"paged analysis unique"`. The hang may correlate with the unique token or be
intermittent; worth trying both when reproducing.

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

- [x] `--screenshot-size WxH` writes a BMP at the requested size for the score host.
- [x] `.musicxml` sources screenshot identically to `.mxl`.
- [x] A regression note in the do.py smoke or a headless test covering one sized capture.

## Status 2026-07-19 — RESOLVED (already fixed upstream; regression guard added)

```
DEBUG REPORT
Symptom:    score host hung under --screenshot-size and for uncompressed .musicxml
            + --screenshot (log reached 'engraved N page(s)' then nothing; killed).
Root cause: the readiness stall the card hypothesized lived in the score host's
            engrave/stream path. The ScoreView host decomposition (async double-buffer
            WindowEngraver with latest-wins generation checks + the ScoreViewModel/
            ScorePresentation split, commit 94bf825 and the C0-C6 composer series)
            reworked exactly that path and resolved the stall. No standalone code fix
            remained to make on the current tree.
Evidence:   both previously-hanging variants now complete rc=0 with correct output:
            .musicxml --screenshot -> 23 MB PNG; --screenshot-size 900x1500 ->
            5,400,054 bytes = 900*1500*4 + 54-byte BMP header (byte-exact size).
Guard:      added `do.py score-shot-check` (do.py) — runs the score host headless with
            --screenshot-size 640x900 from the .musicxml fixture under a 90s timeout and
            requires a byte-exact 2,304,054-byte BMP. A hang, non-zero exit, or wrong
            size fails. Passes on the current tree.
Status:     DONE — verified fixed on macOS + regression guard in place.
```
