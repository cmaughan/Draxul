# Declarative configuration schema

**Type:** refactor
**Priority:** 21
**Raised by:** Claude, GPT/Codex; supported by Gemini

## Goal

Replace the hand-synchronized config key lists, parse/apply/serialize branches, range checks, and documentation tables with one typed schema while preserving TOML comments and unknown module tables.

## Implementation plan

- [ ] Land item 07 first and capture its parity tests as the behavioral baseline.
- [ ] Define descriptors for key path, type, default, target field/accessor, validation/range, persistence policy, reload policy, and user-facing description.
- [ ] Generate or drive known-key checks, parse/validation, serialization, and core-merge behavior from the descriptors.
- [ ] Keep custom parsers only for compound values such as colors, keybindings, and font paths; register them through the same schema.
- [ ] Add nested schema groups for terminal, markdown, chrome, and modules without making `draxul-config` depend on product modules.
- [ ] Generate the configuration tables in `docs/features.md` or a checked fragment from the schema.
- [ ] Migrate core fields in small groups and delete old lists only after parity tests pass.
- [ ] Provide a documented extension hook for SatView/MegaCity settings rather than adding their fields to the core library.

## Tests and acceptance

- [ ] Snapshot defaults and serialized output before migration; require semantic parity after each group.
- [ ] Assert every `AppConfig` field is represented exactly once or explicitly marked runtime-only.
- [ ] Assert generated docs are current in CTest/CI.
- [ ] Unknown comments/tables survive round trips.
- [ ] Build/tests/smoke pass with optional modules on and off.

## Dependencies and parallelism

Depends on item 07. One config owner should control the schema; module-specific migrations can be delegated after the extension API is stable.

<model>GPT-5 Codex</model>
