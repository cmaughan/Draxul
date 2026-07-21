# First-run health center

**Type:** feature
**Priority:** 45
**Raised by:** GPT/Codex

## User need

Provide actionable checks for Neovim, renderer/shaders, fonts, shells, config/session paths, optional module assets, and network/cache status instead of discovering failures piecemeal.

## Implementation plan

- [ ] Define pure health-check results with id, severity, summary, details, remediation action, duration, and cacheability.
- [ ] Add checks for config parse, writable app-data paths, bundled/selected fonts, Neovim/shell discovery, renderer/shader assets, optional module assets, and item 00 transport/caches.
- [ ] Run cheap checks synchronously and expensive/network/GPU checks asynchronously with cancellation and timeouts.
- [ ] Present results in an ImGui/native diagnostics surface available on first run, safe mode, and a palette action.
- [ ] Provide safe actions such as Open Config, Open Log Folder, Retry, Copy Details, and Clear Cache; no arbitrary shell commands.
- [ ] Store only check timestamps/results, not sensitive environment contents.
- [ ] Integrate safe-mode explanation from item 41 when available.

## Tests and acceptance

- [ ] Fake every pass/warn/fail/timeout path and verify deterministic ordering/remediation tokens.
- [ ] Test cancellation/shutdown and first-run marker behavior.
- [ ] A clean install reports ready; missing prerequisites identify the exact path/tool and next action.
- [ ] Update docs; build/tests/smoke pass on both platforms.

## Dependencies and parallelism

Depends on stable config/network contracts (00, 07/21) and complements item 41. Checks can be delegated by subsystem after the result API lands.

<model>GPT-5 Codex</model>
