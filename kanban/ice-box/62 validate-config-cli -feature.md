# Validate configuration without launching the GUI

**Type:** feature
**Priority:** 62
**Raised by:** Gemini

## User need

Run `draxul --validate-config [path]` to check syntax, types, ranges, unknown keys, deprecated keys, and optional-module settings without opening a window or starting a host.

## Implementation plan

- [ ] Add a CLI mode parsed before app/window/renderer initialization with an optional path and machine-readable output flag.
- [ ] Expose config loading as a side-effect-free validation report containing severity, key path, source line/column where available, message, and suggested correction.
- [ ] Drive validation from `kanban/done/21 declarative-config-schema -refactor.md` so runtime and CLI cannot disagree.
- [ ] Derive the valid key-path inventory from that schema and registered
      optional-module fragments; do not maintain a second hand-written list for
      typo matching.
- [ ] Add a small bounded edit-distance helper for unknown keys. Offer a
      correction only when there is one unique closest key in the same schema
      section and its distance is at most 3 and at most 30% of the longer key
      length. Ties or distant names produce no suggestion.
- [ ] Include the suggested correction in the shared diagnostic message used by
      both application startup and CLI text/JSON output, for example:
      `Unknown config key 'scrool_speed' (did you mean 'scroll_speed'?)`.
- [ ] Load optional-module schema fragments according to the same build/provider registration policy as the application.
- [ ] Return stable exit codes for valid, warnings-only, invalid, unreadable, and internal failure.
- [ ] Print resolved config path and defaults/override context without writing or normalizing the file.
- [ ] Document use in CI/editor hooks and keep secrets/values out of diagnostics unless needed.

## Tests and acceptance

- [ ] CLI tests cover missing/default/explicit path, valid config, syntax error, wrong type, range, unknown/deprecated key, duplicate key, module enabled/disabled, JSON output, and no-write behavior.
- [ ] Validation messages match application startup for the same input.
- [ ] Shared schema tests prove `scrool_speed` suggests `scroll_speed`,
      `enabble_ligatures` suggests `enable_ligatures`, and `zzz_nonexistent`
      emits the plain unknown-key warning. Cover nested/section-local matching,
      an equal-distance tie, and a valid optional-module key so suggestions do
      not cross schema ownership boundaries.
- [ ] CLI text and JSON expose the same optional correction as startup without
      changing warning severity or silently rewriting the file.
- [ ] A test factory proves no window, renderer, SDL video, host, network service, or session owner initializes.
- [ ] Windows and macOS return the same documented exit codes.

## Dependencies and parallelism

The declarative schema/result boundaries are delivered. The CLI wrapper is small;
schema/report work should remain with the config owner.

This card subsumes the former
`30 config-typo-suggestions -feature.md`: actionable typo diagnostics belong in
the shared validation result so startup and `--validate-config` cannot drift.

<model>GPT-5 Codex</model>
