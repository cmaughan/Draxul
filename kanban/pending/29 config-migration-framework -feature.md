# WI 29 — Config migration framework

**Type:** feature  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 7

---

## Goal

Version the `config.toml` schema with a `config_version` field. When an older config is loaded, automatically migrate it to the current schema, log the migration steps, and offer to save the migrated config.

---

## Problem

When config fields are renamed, removed, or have changed semantics (e.g. `enable_ligatures` being split into per-feature flags, or the upcoming `[chrome]` section from WI 03), users with existing `config.toml` files get silent unknown-key warnings or wrong defaults. There is no migration path; users must manually update their config or use defaults.

Claude flagged this as #8 in "Best 10 Features to Add."

---

## Implementation Plan

**Phase A — versioning:**
- [ ] Add `config_version = 1` as a new field in `config.toml` (and document it as optional — missing = version 0).
- [ ] Update `AppConfig` to read and store the config version.
- [ ] Define a `ConfigMigrator` class with a `migrate(int from_version, toml::table&) -> Result<int, MigrationError>` API.

**Phase B — first migration (0 → 1):**
- [ ] Identify any fields that have been renamed or removed since Draxul's first public config format.
- [ ] Implement the 0 → 1 migration (even if it's a no-op — establishes the pattern).
- [ ] Log each migration step at INFO level: `"Config: migrated 'old_key' → 'new_key'"`.

**Phase C — user prompting:**
- [ ] After migration, show a toast: `"config.toml updated from version 0 → 1. A backup was saved to config.toml.bak."`.
- [ ] Write the migrated TOML back to disk only with user confirmation (or provide a `--migrate-config` CLI flag for headless use).

**Phase D — future-proofing:**
- [ ] Add a migration test that loads a v0 config string, runs migration, and asserts the result matches a v1 expected string.

---

## Notes for the agent

- Keep migrations additive: never delete old fields without a migration step that explicitly handles them.
- The `ConfigMigrator` should live in `libs/draxul-config/`.

---

## Interdependencies

- **Requires WI 24 (unified Result type)** for clean error returns from migration steps.
- WI 37 icebox (hierarchical-config) will need migration support — coordinate when that work begins.
- WI 03 (chromehost-hardcoded-theme-colors, active) introduces new `[chrome]` config fields; migration from configs without it is the first real test case.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
