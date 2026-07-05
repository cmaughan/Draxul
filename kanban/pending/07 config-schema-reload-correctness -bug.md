# Config schema and reload correctness

**Type:** bug
**Priority:** 07
**Raised by:** GPT/Codex, Claude, Gemini

## Problem

Config behavior is split across hand-maintained known/core key lists, parse/apply/serialize bodies, and App reload code. Current review evidence includes duplicate terminal-key parsing, chrome not participating consistently in core merges, parse failures that are not always surfaced uniformly, weather changes ignored by reload, and partial runtime application when a font change fails.

## Implementation plan

- [ ] Write a field-by-field inventory for `AppConfig`, nested tables, known keys, core merge keys, serializer output, and live-reload consumers.
- [ ] Remove duplicate parsing for terminal URL/OSC/shell-mark keys.
- [ ] Make `[chrome]`, `[terminal]`, `[markdown]`, and every scalar participate deliberately in merge/preservation; add comments for intentionally non-core module tables.
- [ ] Return structured parse/validation errors with file/line context and surface reload failure through a toast while retaining the prior config.
- [ ] Stage font/text-service changes before committing the new config so a failed font load does not partially mutate unrelated runtime state.
- [ ] Add explicit config-diff handling for WeatherService: add starts it, change replaces it, clear stops it.
- [ ] Preserve unknown module tables and comments according to the existing document policy.

## Tests

- [ ] Add parse/merge/serialize parity coverage for every config field and nested table.
- [ ] Add malformed TOML and failed-font reload tests proving the old runtime config remains active.
- [ ] Add weather add/change/clear reload tests with a fake transport and no leaked worker.
- [ ] Add unknown-key/table preservation cases.

## Acceptance criteria

- [ ] No key is parsed twice or silently omitted from its documented merge policy.
- [ ] Reload is all-or-old for validated runtime state.
- [ ] Weather follows the current config after every successful reload.
- [ ] Config tests, `ctest`, and smoke pass.

## Dependencies and parallelism

Land as a narrow correctness patch before `21 declarative-config-schema -refactor.md`. Avoid concurrent edits to `app_config_io.cpp` with SatView config work.

<model>GPT-5 Codex</model>
