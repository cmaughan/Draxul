# Terminal selection auto-scroll

**Type:** feature
**Priority:** 63
**Raised by:** Gemini

## User need

While dragging a terminal selection beyond the pane's top or bottom edge, scroll through history automatically and continue extending the selection.

## Implementation plan

- [ ] Add a pure edge-scroll model that maps pointer distance beyond the content viewport to a capped cells-per-second velocity with a small dead zone.
- [ ] Start auto-scroll only during an active local terminal selection; stop on release, focus loss, pane change, mouse-mode capture, alt screen without history, or pointer return.
- [ ] Tick from the main frame loop using elapsed time, update scrollback viewport, and extend the selection endpoint in buffer coordinates after each scroll step.
- [ ] Keep selection anchors stable across ring-buffer indices and wide/continuation cells; clamp at available history/current grid limits.
- [ ] Request frames only while motion is active and apply configured smooth-scroll policy without accumulating unrelated wheel residue.
- [ ] Add optional speed configuration only after default behavior is validated; use the declarative schema if exposed.

## Tests and acceptance

- [ ] Test top/bottom direction, distance/speed caps, fractional time, history limits, wide cells, resize during drag, mouse protocol enabled, alt screen, focus loss, and release.
- [ ] Selection text after auto-scroll matches the visible buffer range and never indexes outside scrollback/grid storage.
- [ ] Idle CPU/frame behavior is unchanged when no drag is active.
- [ ] Validate manually on both platforms with mouse and trackpad drags.

## Dependencies and parallelism

Touches InputDispatcher and terminal selection/scrollback; one interaction owner should integrate it after existing tab/input work settles. It is unrelated to the proposed but unplanned terminal scrollbar.

<model>GPT-5 Codex</model>
