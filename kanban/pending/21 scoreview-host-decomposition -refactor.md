# Decompose ScoreView host responsibilities

**Type:** refactor
**Priority:** 21
**Raised by:** GPT/Codex, Claude

## Goal

Reduce the 2,778-line `score_host.cpp` collision surface while preserving the `IHost`/provider contract, ScoreView behavior, NanoVG rendering, progress compatibility, and cross-platform audio/device paths.

## Target boundaries

- `ScoreSessionController`: source loading, analysis cache, progress paths/load/save, and session recap data.
- `ScoreStreamController`: SourceSlicer/composer, rolling-window generations, FlowController carry/verdict archive, rebuild policy, and transport state.
- `ScoreAudioController`: output stream, metronome/audition/instrument mixing, player-input selection, microphone/MIDI lifecycle, and device changes.
- `ScoreViewModel`: immutable per-frame status/inspector/presentation data and user intents.
- `ScorePresentation`: score/waterfall/keyboard NanoVG recording, layout, and hit testing; no device or persistence ownership.
- `ScoreHost`: thin lifecycle/input orchestrator connecting these components to Draxul interfaces.

## Status 2026-07-16

Slices landed via cards 20/22 + three extractions here (each committed green:
build, scoreview ctest suites, smoke). ScoreHost is down from 2,778 to ~2,150
lines and owns orchestration only: persistence, device details, audio, and
the inspector all live in components. What remains is the formal
`ScoreStreamController` wrapper and the ViewModel/Presentation snapshot —
the stream's constituent parts (slicer, IComposer+StreamProgram,
VerdictArchive, WindowEngraver with request-id generations) are already
extracted, separately-tested components; only the host-side wiring between
them and the presentation swap is not yet boxed.

## Implementation plan

- [x] Bugs 00/14 landed first (their suites exist: scoreview_microphone,
      scoreview_host_rebuild, plus the ScoreHostTestAccess seam). Tests 15/16
      remain open as their own cards; the 18 existing scoreview suites +
      smoke served as the behavioral baseline for these slices.
- [x] Inventoried per slice; no state duplicated — each member moved to
      exactly one owner (audio rig, session controller, input rig, archive,
      program, composer).
- [x] Controllers are main-thread direct-call components; the only worker
      remains WindowEngraver (already generation-checked). No GPU/NanoVG work
      left the main thread.
- [x] `ScoreSessionController` extracted (src/score_session_controller.{h,cpp}):
      player model, content-hash progress file, session clock, bar-boundary
      flush, cached piece analysis + dump. Host keeps only tempo
      resume/record (transport-coupled).
- [ ] Formal `ScoreStreamController`: the components it would own are all
      extracted (SourceSlicer, IComposer + host-owned StreamProgram,
      VerdictArchive, WindowEngraver); remaining work is boxing the host's
      window bookkeeping (first/count/offset, advance policy,
      build_window_slice, pending-install routing) behind one seam so it can
      be driven by the card-15 fixture without the whole host.
- [x] `ScoreAudioController` extracted (src/score_audio_controller.{h,cpp}):
      output stream, metronome, audition, instrument voices, MIDI play-thru,
      soundfont staging. SDL audio types never cross into the host.
      `PlayerInputRig` (card 22) keeps keyboard/mic/MIDI concrete types out
      of the host; platform types stay private.
- [ ] Presentation input model (`ScoreViewModel`/`ScorePresentation`): the
      inspector now lives in its own TU and reads through controller getters
      (close to a snapshot, not yet a formal one); NVG recording already
      lives in score_render_nvg/keyboard_render_nvg. Formal snapshot types
      remain follow-up, best done with the card-15 fixture in hand.
- [x] Behavior-preserving slices, tests green after every slice, one commit
      per slice; no parallel feature work in `score_host.cpp`.
- [x] Source-file/target splits used internal headers only (`src/*.h`) — no
      public headers for implementation-only components.
- [x] `docs/features.md` updated (five-library layout + internal components);
      plans/scoreview-stream.md S3 notes updated via card 20.

## Tests and acceptance

- [x] All existing ScoreView tests remained green after every extraction
      (items 15/16 still open as their own cards).
- [x] Progress files, command tokens, keybindings, status text, and device
      fallback behavior preserved (verbatim code motion; token semantics for
      tick/notes/composer unchanged, verified by suites + smoke).
- [x] ScoreHost no longer owns persistence, device details, audio mixing, or
      the inspector; WindowEngraver owns the worker thread/generations and
      the host only routes completions into the presentation swap.
- [x] Session/audio/input-rig/archive/program/composer components are each
      constructible and testable without the host (archive/program/composer/
      measure-writer have dedicated suites; session/audio are host-free
      classes awaiting the card-15 fixture for lifecycle coverage).
- [ ] Windows/Vulkan build validation of the new CMake layout (macOS build +
      smoke + unit suites verified here; the layout has no platform
      branches, but CI should confirm before this card closes).

## Dependencies and parallelism

Depends on new bugs 00/14 and tests 15/16; coordinate with pending 35 modular test targets. The composer/program seams land first in `20 scoreview-composer-decoupling -refactor.md` so `ScoreStreamController` is built around `StreamProgram` provenance rather than re-extracting it. One integration owner should sequence extractions. After interfaces stabilize, separate agents can own session/stream, audio, and presentation migrations, but they must not edit `score_host.cpp` concurrently without that owner.

<model>GPT-5 Codex</model>
