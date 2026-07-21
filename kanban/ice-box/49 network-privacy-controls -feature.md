# Network, offline, privacy, and cache controls

**Type:** feature
**Priority:** 49
**Raised by:** GPT/Codex

## User need

Give users explicit control over weather and SatView network access, refresh policy, proxy use, source disclosure, cache inspection, and clearing downloaded data.

## Implementation plan

- [ ] Land shared transport item 00 and declarative config item 21.
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
- [ ] Weather and SatView work fully from bundled/last-good data in offline mode.

## Dependencies and parallelism

Depends on items 00 and 21; coordinates with items 41 and 45. A network/UI pair of sub-agents can split after service status APIs are fixed.

<model>GPT-5 Codex</model>
