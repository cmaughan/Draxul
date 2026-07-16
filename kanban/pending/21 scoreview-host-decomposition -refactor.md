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

## Implementation plan

- [ ] Land bugs 00/14 and tests 15/16 first; record current host-level behavior as the baseline.
- [ ] Inventory every ScoreHost field/method and assign one owner; do not duplicate state during migration.
- [ ] Define narrow command/event records between controllers and the main-thread host; keep all GPU/NanoVG work on the main thread.
- [ ] Extract `ScoreSessionController` and `ScoreStreamController` first because their core dependencies are already GPU-free.
- [ ] Extract `ScoreAudioController` only after the injected device contracts are stable; platform/SDL/RtMidi types stay private.
- [ ] Build a presentation input model so ImGui/NanoVG draw functions read snapshots rather than mutating controllers directly.
- [ ] Move code in behavior-preserving slices with host tests green after every slice; avoid simultaneous feature work in `score_host.cpp`.
- [ ] Split source files/CMake target internals without creating public headers for implementation-only components.
- [ ] Update ScoreView architecture docs and `docs/features.md` summary after the boundary is real.

## Tests and acceptance

- [ ] Items 15/16 and all existing ScoreView tests remain green after every extraction.
- [ ] Progress files, command tokens, keybindings, status text, render output, and device fallback behavior are compatible.
- [ ] ScoreHost owns lifecycle orchestration, not persistence, worker state machines, device details, or large draw implementations.
- [ ] Each controller can be tested without constructing the entire host; optional-off builds remain clean.
- [ ] Build/smoke/render validation passes on Metal and Vulkan paths.

## Dependencies and parallelism

Depends on new bugs 00/14 and tests 15/16; coordinate with pending 35 modular test targets. One integration owner should sequence extractions. After interfaces stabilize, separate agents can own session/stream, audio, and presentation migrations, but they must not edit `score_host.cpp` concurrently without that owner.

<model>GPT-5 Codex</model>
