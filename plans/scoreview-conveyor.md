# ScoreView Conveyor — manifesto milestone 1

*Created 2026-07-11. Parent: [scoreview.md](scoreview.md) · North star:
[scoreview-manifesto.md](scoreview-manifesto.md).*

The endless runner's chassis: the **flowing single-row view** with a
**transport clock** and **timemap-driven note light-up**. No audio, no game
logic, no adaptive selection yet — this milestone builds and proves the
moving conveyor those will ride on.

## The demo (what "done" looks like)

`draxul --host score --source grieg-waltz-op-12-no-2.mxl`, press `f`:

- The paged score becomes a **single flowing row** of the whole piece,
  vertically centered on the backdrop, several measures visible (zoom via the
  existing font actions; default shows well over 2 bars).
- `Space` starts the transport at a **slow default (≈60% of the piece's
  marking)**. The strip scrolls smoothly leftward beneath a **fixed playhead**
  anchored ~30% from the left edge.
- As the playhead crosses each note's onset, the note **lights up** (accent
  color) and stays lit — the manifesto's "correct notes light up" rendered
  end-to-end, driven by the clock instead of the player for now.
- `[` / `]` nudge the tempo down/up — clamped to **max 120% of the marking**
  (manifesto cap) and a sane floor; `r` rewinds; `Space` pauses; `f` returns
  to paged mode. The status pill shows tempo, measure, and progress.

Milestone 2 then replaces "the clock advances position" with "the matched
player input advances position" — the seam is designed for exactly that swap.

## Verified foundations (measured 2026-07-11, Verovio 6.2.1, Grieg fixture)

- `breaks: "none"` + `adjustPageWidth/Height` → **one page, viewBox
  `0 0 8082 258`** — the whole 79-measure waltz as one strip, in the exact
  SVG dialect the interpreter already covers (no new constructs; ~2.9k draw
  ops total, *fewer* than the old densest single page).
- `RenderToTimemap()` → JSON array, **308 entries** of
  `{qstamp, tstamp, on: [ids], off: [ids]}`, plus **`tempo: 130`** on the
  first entry — the piece's marking, programmatically, for the tempo cap.
- **Perfect id join**: all 645 unique note-on ids exist as element ids in the
  flow SVG → our draw-list `element_id`s. And 645 note-ons + 49 rests = 694,
  matching the importer's independently-counted note total exactly.
- Final `qstamp` 237 = 79 measures × 3 quarters — quarter-note time is exact
  and clean as the canonical transport axis.

## Architecture

```
VerovioLayoutEngine (flow mode)  ──►  SVG (1 page)  ──►  ScoreDrawList (ids)
        │                                                       │
        └──►  render_timemap() JSON  ──►  Timemap ──────────────┤
                                                                ▼
                                    FlowController  (GPU-free, unit-tested)
                                      transport (position_q, tempo_qpm)
                                      qstamp ⇄ x piecewise mapping
                                      lit-note set diffs
                                                                │
                                    ScoreHost (flow mode)  ◄────┘
                                      pump(): advance by wall dt, request_frame
                                      draw(): strip @ -scroll_x, playhead,
                                              highlight overlay via NanoVG
```

- **Time axis is `qstamp`** (quarter-note time). Wall time enters only in
  `advance(dt × tempo_qpm/60)`. The timemap's `tstamp` is unused except to
  read the marking — the runner owns its own tempo, always.
- **Engraved spacing, variable scroll speed** (per the research note and
  manifesto): x-position at time t comes from piecewise-linear interpolation
  between consecutive onset x's, so the conveyor breathes with the notation
  instead of grinding at constant px/s.
- **Highlighting is an overlay, not a draw-list mutation**: at prepare time
  build `element_id → op indices` buckets; the controller emits lit/unlit id
  diffs; a parallel per-op flag vector feeds `render_draw_list`, which picks
  accent vs ink per op. No per-frame string lookups, no relayout.
- **JSON parsing via nlohmann/json** (FetchContent, pinned, PRIVATE to
  draxul-scoreview). Deliberately not Verovio's vendored jsonxx — same
  ODR/vendored-symbol reasoning as the tinyxml2-not-pugixml decision.
- **Re-layout invalidates everything together**: element ids and timemap are
  per-load/per-layout snapshots; after any flow re-layout (resize, zoom) the
  join is rebuilt from the fresh SVG + fresh timemap, so it is consistent by
  construction.

## Non-goals (later milestones)

Microphone input and matching; wait-for-player mode; scoring/streaks UI;
adaptive tempo from performance; the selection engine (hands/simplified/
motifs); model↔layout id bridge; memory-mode notation fading.

## Phases

### C0 — Flow layout + timemap in the engine ✅ (2026-07-11)

- [x] `LayoutOptions.mode` (`Paged` | `Flow`): flow sets `breaks: "none"`,
  `adjustPageWidth/adjustPageHeight`, `header: "none"`, and derives scale
  from a target strip height instead of page fit
- [x] `ILayoutEngine::render_timemap()` → JSON string (Verovio
  `RenderToTimemap`)
- [x] `score_timemap.h/.cpp`: nlohmann-parsed `Timemap { entries(qstamp,
  on[], off[]), tempo_qpm, duration_q }`; tolerant of missing keys
- [x] nlohmann/json in FetchDependencies (pinned, scoreview-gated)
- [x] Tests: flow mode → page_count 1 + wide canvas + **zero interpreter
  warnings** on the Grieg; timemap parses (308 entries, tempo 130,
  duration 237); **id-join test: every timemap on-id resolves to ≥1 draw op**

### C1 — FlowController (pure logic) ✅ (2026-07-11)

- [x] Join Timemap × ScoreDrawList → ordered onset events `{qstamp, x,
  ids[]}` (x = the id's op bucket x-position)
- [x] Transport: play/pause/rewind, `advance(dt)`, `position_q` clamped to
  [0, duration]; tempo get/set clamped to [marking×0.25, marking×1.2],
  default start marking×0.6
- [x] `x_at(position_q)` piecewise-linear between onsets (flat before first /
  after last); `scroll_x(viewport_w, anchor_frac)` with start/end clamping
- [x] Lit-set maintenance: `advance_to(q)` emits newly-lit op indices
  (monotonic path), `rewind` resets; idempotent re-application
- [x] Unit tests: clamps, interpolation (including between-onset midpoints),
  lit diffs across forward jumps and rewind, tempo bounds from the marking

### C2 — Highlight overlay in the renderer ✅ (2026-07-11)

- [x] `ScoreHighlightState`: id→op-index buckets built once per interpret;
  per-op lit flags; accent color constant (warm amber, distinct from ink on
  the page white)
- [x] `render_draw_list` accepts an optional highlight state (lit ops draw in
  accent for fills/strokes/glyphs; texts excluded for now)
- [x] Unit test: bucket construction over the fixture (notehead op count for
  a known id), flag application

### C3 — ScoreHost flow mode ✅ (2026-07-11)

- [x] Mode toggle `f` (paged ⇄ flow), preserving the paged scroll position
  for the return trip
- [x] Flow layout path: one interpret, strip centered vertically, horizontal
  scroll from the controller, playhead line drawn at the anchor
- [x] Transport keys: `Space` play/pause, `[`/`]` tempo ∓ (e.g. 4%/step),
  `r` rewind; while playing, pump advances by steady-clock dt and requests
  continuous frames; paused = dirty-flag redraws as today
- [x] Status text in flow mode: `▶ ♩=78 (60%)  m. 12  q 34.5/237`
- [x] Smoke: `--host score --source grieg …` toggles to flow and back without
  leaks or warnings

### C4 — Verification & polish ✅ (2026-07-11)

- [x] Objective motion proof: startup-autoplay hook for testing (config key
  or host arg — decide at implementation), two `--screenshot` captures at
  different delays, assert pixel difference in the strip region and lit
  accent pixels appearing (the established capture-and-sample loop)
- [x] Frame cost: the full strip is ~2.9k ops — the same magnitude the paged
  view already replayed per frame without trouble — so no culling was added.
  Revisit with a real measurement if flow scrolling ever feels heavy
- [x] `docs/features.md` + plan ticks. Tempo band (0.25–1.2 × marking, 0.6
  start), the 4%/step nudge, and the 0.3 playhead anchor live as named
  constants in FlowController/ScoreHost; config exposure deferred until the
  game milestones decide what players actually tune

## Acceptance

The demo scenario works on the Grieg end-to-end; zero interpreter warnings in
flow mode; the id-join test pins 100% resolution; all new unit suites green
alongside the full suite and smoke; screenshot diff proves motion and
light-up without a human in the loop (plus the user's eyeball pass for feel).

## Risks / notes

- **Canvas width in float**: the Grieg strip is ~81k canvas units; a 30-min
  piece might reach ~1M units. Float precision at 1M with sub-unit accuracy
  is fine (24-bit mantissa ≈ 16M), but keep scroll math in doubles in the
  controller as cheap insurance.
- **Grace notes / ornaments in the timemap**: grace notes may share a qstamp
  with their principal — the join treats simultaneous ids as one event;
  verify against the Grieg's 14 grace notes in C1 tests.
- **Very long pieces**: one draw list for everything is O(piece) memory
  (fine for real piano literature); the selection-engine milestone will
  regenerate short streams anyway, which bounds this by design.
- **Repeats/jumps**: MusicXML repeats make score-order ≠ playback-order;
  the timemap follows playback order (qstamp can revisit measures). C1
  treats qstamp as monotonic input — if the Grieg's repeats surface
  non-monotonic x, clamp to forward motion for this milestone and record the
  proper repeat-aware conveyor as a follow-up.
