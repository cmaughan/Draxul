# Split with host launcher

**Type:** feature
**Priority:** 69
**Raised by:** Claude, Gemini

## User need

Create a split and choose Nvim, shell, Markdown, ScoreView, SatView, or another available provider instead of always launching the platform shell.

## Implementation plan

- [ ] Extend provider metadata with display name, description, source/path requirements, default launch options, icon/category, and availability reason.
- [ ] Add `PaneManager::split_with_descriptor(direction, descriptor)` as a transaction that validates and initializes the host before committing the split tree.
- [ ] Register “Split with host…” in the structured palette, first choosing direction/provider and then completing required source/path/working-directory arguments.
- [ ] Offer a compact quick-launch grid as an alternate presentation over the same provider/descriptor model, not a second hardcoded host list.
- [ ] Preserve current `split_vertical`/`split_horizontal` actions as fast shell defaults.
- [ ] Roll back renderer/pass registration and topology on initialization failure and show provider-specific errors.
- [ ] Persist the resulting launch descriptor through normal sessions and remember recent providers only if the MRU ice-box decision changes.

## Tests and acceptance

- [ ] Test every enabled provider, optional-off builds, missing required source, invalid direction, partial init failure, focus, viewport, and session restore.
- [ ] Palette and quick grid show exactly the providers currently registered and available.
- [ ] Failed launch leaves the original tree/host/focus unchanged and leaks no pass/device/process.
- [ ] Existing shell split shortcuts are unchanged.

## Dependencies and parallelism

Depends on pending 12, 22, and structured palette item 58. Share provider picking with type-aware drops (52) and scaffolding (38). One PaneManager integration owner should land the transactional split API.

<model>GPT-5 Codex</model>
