# Follow system appearance

**Type:** feature
**Priority:** 72
**Raised by:** Claude

## User need

Optionally follow macOS/Windows light/dark appearance and switch Draxul Chrome colors without restarting.

## Implementation plan

- [ ] Add a platform-neutral `SystemAppearance { Light, Dark, Unknown }` query/change
      event to `IWindow`, using the existing guarded callback lifetime mechanism.
- [ ] Add `follow_system_appearance` plus named light/dark Chrome palette definitions through the declarative config schema; default off to preserve existing themes.
- [ ] Separate semantic Chrome color roles from the currently active palette and apply a complete palette atomically on appearance change.
- [ ] Notify Chrome, overlays, ImGui styling, and hosts that consume theme roles, then request one relayout/frame only where metrics change.
- [ ] Define Unknown/high-contrast handling and coordinate with accessibility mode rather than overriding it silently.
- [ ] Preserve user custom palettes; following selects between them and never rewrites `config.toml` on each OS change.

## Tests and acceptance

- [ ] Fake-window tests cover startup light/dark/unknown, runtime changes, rapid changes, config reload, follow off, custom palettes, high contrast, and callback teardown.
- [ ] All semantic roles update together without one-frame mixed palettes.
- [ ] Platform registrations are instance-safe and no callback fires after window/App teardown.
- [ ] Visual smoke/snapshots validate readable Chrome/overlay contrast on Metal and Vulkan.

## Dependencies and parallelism

The config schema and Chrome layout split are complete. Coordinate with
`kanban/ice-box/46 accessibility-mode -feature.md`; platform adapters can be separate
tasks after the neutral event contract is fixed.

<model>GPT-5 Codex</model>
