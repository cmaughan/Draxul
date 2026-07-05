# Portable profile bundle import/export

**Type:** feature
**Priority:** 48
**Raised by:** GPT/Codex

## User need

Export/import config, keybindings, theme, shell defaults, and selected session layouts as a versioned portable archive without leaking secrets or machine-specific paths.

## Implementation plan

- [ ] Define a versioned manifest with included components, source platform/app version, hashes, and explicit exclusions.
- [ ] Build export from parsed config/session models, not blind directory zipping.
- [ ] Classify paths/values as portable, relativizable, machine-specific, or sensitive; omit/redact by default and show a review summary.
- [ ] Write bundles atomically and validate archive entry paths/sizes/hashes before import to prevent traversal or resource abuse.
- [ ] Import into a staged model, show conflicts/diff, and support merge versus replace with rollback.
- [ ] Reuse config schema migrations and atomic persistence; do not import caches, logs, downloaded data, or live owner metadata.
- [ ] Add CLI and GUI actions only after the core pure import/export API is tested.

## Tests and acceptance

- [ ] Round-trip representative Windows/macOS profiles and selected shell sessions.
- [ ] Test future/old versions, corrupt hashes, duplicate/traversal entries, oversized archives, conflicts, and rollback.
- [ ] Scan fixture bundles to prove secrets/machine paths are excluded by default.
- [ ] Update docs and run config/session tests/smoke.

## Dependencies and parallelism

Depends on items 02 and 21; benefits from item 40 for session data ownership. Core archive work can be delegated after the manifest is approved.

<model>GPT-5 Codex</model>
