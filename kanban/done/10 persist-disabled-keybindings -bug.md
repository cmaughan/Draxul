# Preserve explicitly disabled default keybindings
**Severity:** HIGH
**Source:** Codex #11; `libs/draxul-config/src/app_config_io.cpp:376`.

Removed bindings are omitted during serialization, so shutdown saving erases explicit disables and defaults return at restart.

**Investigation**

- [x] Trace empty binding parsing, serialization, document merging, and shutdown persistence.

**Fix strategy**

- [x] Serialize explicit empty entries for disabled default actions while preserving configured chords and unrelated settings.

**Acceptance criteria**

- [x] `copy = ""` remains disabled after serialization, document merge, and restart.
- [x] Verify enabled bindings still round-trip; run core aggregate tests and same-cache smoke.

**Implementation and validation notes (2026-09-25):** Parsing removes an empty
entry from the in-memory action list. The serializer previously emitted only
present actions, so `ConfigDocument::merge_core_config` and shutdown saving
removed the explicit disable. The writer now emits an empty TOML string for every known action
missing from that list, preserving a deliberate disable across parse, merge,
save and restart. A regression test covers disabled `copy`, a customized
`paste`, and an unrelated plugin-owned table.

The first shared aggregate exposed three config parity goldens that predated
the intended empty-entry format. They were regenerated with the documented
`DRAXUL_REGEN_CONFIG_GOLDENS=1` parity-test workflow on 2026-09-25; the diff is
limited to the same 24 newly emitted disabled action keys in each fixture.
The regeneration run passed 5 cases and 44 assertions. A pre-existing test
that searched for the broad substring `terminal` was narrowed to the actual
`[terminal]` section, since `take_terminal_control` is now a serialized key.

Final Windows validation: `py do.py test debug --products` passed 49/49 CTest
entries in 238.19 seconds, including the config round-trip and parity cases.
The standard fixed-30-second smoke timed out loading the existing nine-pane
Session; the same-cache smoke in the exact wrapper environment with a
90-second bound passed in approximately 45 seconds.
