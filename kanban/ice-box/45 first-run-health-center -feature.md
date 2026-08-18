# First-run health center

**Type:** feature
**Priority:** 45
**Raised by:** GPT/Codex

## User need

Provide actionable checks for Neovim, renderer/shaders, fonts, shells, config/session paths, optional module assets, and network/cache status instead of discovering failures piecemeal.

## Implementation plan

- [ ] Define pure health-check results with id, severity, summary, details, remediation action, duration, and cacheability.
- [ ] Add core checks for config parse, writable app-data paths, bundled/selected fonts,
      Neovim/shell discovery, renderer/shader assets, and core transport/caches.
- [ ] Define a generic plugin health/status capability so optional products report their
      own assets, data sources, and remediation without core inspecting product paths.
- [ ] Run cheap checks synchronously and expensive/network/GPU checks asynchronously with cancellation and timeouts.
- [ ] Present results in an ImGui/native diagnostics surface available on first run, safe mode, and a palette action.
- [ ] Provide safe actions such as Open Config, Open Log Folder, Retry, Copy Details, and Clear Cache; no arbitrary shell commands.
- [ ] Store only check timestamps/results, not sensitive environment contents.
- [ ] Integrate `kanban/ice-box/41 safe-mode-startup -feature.md` explanation when available.

## Tests and acceptance

- [ ] Fake every pass/warn/fail/timeout path and verify deterministic ordering/remediation tokens.
- [ ] Test cancellation/shutdown and first-run marker behavior.
- [ ] A clean install reports ready; missing prerequisites identify the exact path/tool and next action.
- [ ] Update docs; build/tests/smoke pass on both platforms.

## Dependencies and parallelism

The config, result, and HTTP contracts are available. Coordinates with
`kanban/ice-box/41 safe-mode-startup -feature.md` and
`kanban/ice-box/49 network-privacy-controls -feature.md`.

<model>GPT-5 Codex</model>
