# Network, offline, privacy, and cache controls

**Type:** feature
**Priority:** 49
**Raised by:** GPT/Codex

## User need

Give core services explicit offline/proxy policy and let the built-in weather service
report and clear only its own downloaded data. Product network/cache policy belongs to
the owning plugin.

## Implementation plan

- [x] Reuse the delivered shared HTTP transport and declarative config schema.
- [ ] Add global offline mode plus per-service enable/refresh-policy settings; defaults must preserve current behavior unless product policy changes explicitly.
- [ ] Model proxy/system-proxy policy in the transport without logging credentials.
- [ ] Expose source URL/name, last attempt/success, cache age/size/path, current source (bundled/live/cache), and last error.
- [ ] Add Refresh and Clear Downloaded Data actions with confirmation and cancellation of in-flight work.
- [ ] Clearing cache must be scoped to known service-owned files and preserve bundled assets/attribution.
- [ ] Ensure safe mode forces offline without rewriting the user's normal preference.
- [ ] Document every external source and data retained locally.

## Tests and acceptance

- [ ] Fake transport tests cover global/per-service offline, refresh cadence, proxy policy, clear during request, restart, and cache fallback.
- [ ] File tests prove clear operations cannot escape the cache root.
- [ ] UI/config round trips show accurate state without network access.
- [ ] Weather works from bundled/last-good data in offline mode.

## Dependencies and parallelism

Coordinates with `kanban/ice-box/41 safe-mode-startup -feature.md` and
`kanban/ice-box/45 first-run-health-center -feature.md`. SatView-specific policy is
tracked in `plugins/satview/kanban/ice-box/41 satview-network-privacy-controls -feature.md`.

<model>GPT-5 Codex</model>
