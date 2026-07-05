# Host and module scaffolding commands

**Type:** feature
**Priority:** 38
**Raised by:** Claude

## Developer need

Adding a host/module requires repeated CMake, registration, config, source, and test boilerplate. A generator should create a minimal architecture-compliant skeleton without editing unrelated central switches.

## Implementation plan

- [ ] Land provider metadata item 12 so generated hosts register through the current boundary.
- [ ] Add `py do.py new-host <name> [--module <name>]` and `new-module <name>` with validation and `--dry-run`.
- [ ] Store templates under `scripts/templates/` for public/internal headers, source, registration function, CMake target, and Catch2 test.
- [ ] Generate optional module gating and platform source branches only when requested.
- [ ] Update root CMake/feature option at stable markers using a parser/marker blocks, not unconstrained string replacement.
- [ ] Refuse existing paths/targets and roll back all generated files if any step fails.
- [ ] Print next steps for implementation, docs, optional-off validation, build, tests, and smoke.

## Tests and acceptance

- [ ] Generate core and optional sample hosts in temporary fixture repos and configure them.
- [ ] Test invalid names, collisions, dry-run, rollback, Windows/macOS template selection, and idempotent failure.
- [ ] Generated code follows header placement, GLM, registry, and CMake rules in `AGENTS.md`.
- [ ] No product host enum/list requires manual duplicated editing beyond provider registration.

## Dependencies and parallelism

Depends on item 12 and benefits from item 31. An automation sub-agent can own it once registration conventions stabilize.

<model>GPT-5 Codex</model>
