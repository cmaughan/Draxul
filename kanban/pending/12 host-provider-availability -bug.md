# Derive host choices from provider availability

**Type:** bug
**Priority:** 12
**Raised by:** GPT/Codex, Claude

## Problem

`CommandPalette::open()` hardcodes host kinds independently of `HostProviderRegistry`. Optional-off builds can offer `split_*`/`new_tab` actions that cannot create a host. CLI help, aliases, and factories also drift across separate switches.

## Implementation plan

- [ ] Extend registry entries with stable kind, display name, canonical CLI name/aliases, palette visibility, platform availability, and supported launch context.
- [ ] Add ordered read-only enumeration; preserve current factory lookup and registration boundary.
- [ ] Build command-palette compound actions from registered visible providers only.
- [ ] Derive CLI host help/validation from the same metadata while preserving compatibility aliases.
- [ ] Keep product module registration in `app/main.cpp` behind its feature flag.
- [ ] Treat NanoVG demo as test-only metadata when the render-test isolation card is reopened.

## Tests

- [ ] Add registry/palette/CLI matrices for MegaCity and SatView independently enabled/disabled and for Windows/macOS shell availability.
- [ ] Verify every registered canonical name parses/serializes round-trip.
- [ ] Verify an unregistered optional host is neither advertised nor constructible.

## Acceptance criteria

- [ ] No command-palette host list is hardcoded separately from providers.
- [ ] Optional-off builds expose no impossible host actions.
- [ ] Existing CLI names and aliases remain compatible.
- [ ] Build/test optional-module combinations, `ctest`, and smoke.

## Dependencies and parallelism

Supports item 31 and item 38. Can run separately from renderer/module internals, but it touches central registration files.

<model>GPT-5 Codex</model>
