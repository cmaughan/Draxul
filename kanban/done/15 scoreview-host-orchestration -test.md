# ScoreView host orchestration coverage

**Type:** test
**Priority:** 15
**Raised by:** GPT/Codex, Claude

## Gap

The unit target links `draxul-scoreview` but not `draxul-scoreview-host`. Pure scoring, notation, slicing, and audio algorithms are tested, while the real host's initialization, window installation, input switching, presentation callbacks, resizing, printing hints, and shutdown are not.

## Implementation plan

- [x] Injection seams landed as the friend-seam + component injection pattern
      (ScoreHostTestAccess + DeterministicLayoutEngine + injected
      WindowEngraver + internal controllers tested directly) rather than a
      formal `ScoreHost::Deps` struct — the seam the bug-00/14 fixes
      established, now shared in tests/support/scoreview_host_fixture.h. No
      SDL/Verovio type crosses the test contract.
- [x] Headless fixture: shared header with the blockable fake engine, minimal
      score/timemap, SVG fixture reader, counting IHostCallbacks, temp
      progress dir (via support/temp_dir.h). Fake NanoVG/renderer/clock not
      needed for the covered surface (draw() is not driven; the sourceless
      lifecycle exercises the real pass object headlessly).
- [x] draxul-tests already links draxul-scoreview-host; internal controller
      headers reach tests via a scoped src include dir (final target shape
      stays with pending 35).
- [x] Lifecycle covered: sourceless initialize→pump→status/runtime-state/
      debug-state/print-hint→set_viewport→shutdown (idempotent, from
      never-initialized/failed/primed/pending-generation states); callbacks
      verified silent after shutdown. draw() excluded (needs a frame context —
      the render suite owns that surface).
- [x] Async install + carry state, input selection (requested-vs-engaged),
      resize, print hint, frame requests covered here and in the rebuild
      suite; paged/flow/roll transition matrix remains with the render
      scenarios (needs real pages).
- [x] Failure cases: missing source, worker unavailable (sync fallback,
      rebuild suite), layout failure (FailingLayoutEngine → clean monolith
      fallback + safe shutdown), corrupt progress file (fresh model, still
      saves). Audio-unavailable needs the SDL-audio ops seam (deferred with
      card 70).

## Tests and acceptance

- [x] No test requires a physical GPU, microphone, MIDI keyboard, audio
      output, or user dialog (mic deliberately unreachable from the fixture).
- [x] Delayed fake jobs prove the old window keeps rendering until a valid
      generation arrives (rebuild + stress suites).
- [x] Shutdown idempotent from partial initialization and every covered mode.
- [x] Optional-off builds omit the sources via the existing scoreview glob
      filter; macOS verified through CTest here (Windows via CI).
- [x] The fixture gated item 21's ScoreStreamController extraction: the same
      suites passed unchanged across the refactor, reading state only through
      the accessor seam.

## Dependencies and parallelism

Depends on the injection seams from bugs 00 and 14, and feeds item 21. The fixture/test target is a good test-agent task, but coordinate all `ScoreHost::Deps` changes with the one ScoreHost integration owner.

<model>GPT-5 Codex</model>
