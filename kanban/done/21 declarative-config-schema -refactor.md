# Declarative configuration schema

**Type:** refactor
**Priority:** 21
**Raised by:** Claude, GPT/Codex; supported by Gemini

## Goal

Replace the hand-synchronized config key lists, parse/apply/serialize branches, range checks, and documentation tables with one typed schema while preserving TOML comments and unknown module tables.

## Implementation plan

- [x] Land item 07 first and capture its parity tests as the behavioral baseline.
- [x] Define descriptors for key path, type, default (read back through the accessor), target field/accessor, validation/range, persistence policy (emit rule), and user-facing description. (Reload policy is not a per-field column: reload uses the uniform checked-parse path `parse_app_config_checked`.)
- [x] Generate or drive known-key checks, parse/validation, serialization, and core-merge behavior from the descriptors.
- [x] Keep custom parsers only for compound values such as colors and keybindings; register them through the same schema. (Colors use schema `ValueKind` hooks; keybindings remain the sole compound section parser.)
- [x] Add nested schema groups for terminal, markdown, chrome, and modules without making `draxul-config` depend on product modules.
- [x] Generate the configuration tables in a checked fragment (`docs/config-keys.generated.md`) from the schema. (Left `docs/features.md` untouched -- owned by another agent this session.)
- [x] Migrate core fields in small groups and delete old lists only after parity tests pass.
- [x] Provide a documented extension hook (`register_module_table`) for SatView/MegaCity settings rather than adding their fields to the core library.

## Tests and acceptance

- [x] Snapshot defaults and serialized output before migration; require semantic parity after each group. (`tests/config_parity_tests.cpp` + goldens; held byte-identical through every migration commit.)
- [x] Assert every `AppConfig` field is represented exactly once or explicitly marked runtime-only. (`config_schema_tests.cpp` completeness test.)
- [x] Assert generated docs are current in CTest/CI. (Docs-freshness test in the unit suite, run by the ctest shards.)
- [~] Unknown comments/tables survive round trips. (Unknown module **tables** survive -- pinned by the merged-document golden. **Comments are dropped by toml++** on any round trip; this is pre-existing behavior unchanged by the refactor, not a new regression.)
- [~] Build/tests/smoke pass with optional modules on and off. (Modules **on**: full ctest 11/11 + smoke green at every commit. Modules **off**: `draxul-config` -- the entire change surface, which has no module dependency -- configured and built clean; see Status.)

## Status (2026-07-21)

Implementation is complete. `config_from_toml` now delegates top-level and
section value parsing, range handling, color hooks, and unknown-key warnings to
the schema driver. The legacy one-off behaviors remain pinned: Markdown font
inheritance is applied between the two schema passes, out-of-range
`UseDefault` fields warn where they did before, and an integer literal for
`palette_bg_alpha` remains ignored.

Focused validation: the Release `draxul-config` target builds cleanly. The
monolithic `draxul-tests` rebuild was stopped at the integration owner's request
because concurrent modular-test work needed to reconfigure the shared build
tree. The card remains pending until integration completes the required config,
parity, full-test, and smoke runs.

## Prior migration status (2026-07-19)

Schema infrastructure and the low-risk migrations are **done and fully green**;
the **value-parse / range-validation** group is the remaining work. The item
stays in `kanban/pending/` because of that group.

### Landed this session (7 commits, each build + ctest 11/11 + smoke green)

1. **Parity baseline** -- `tests/config_parity_tests.cpp` + goldens under
   `tests/fixtures/config/` (default, kitchen-sink, merged-document). Held
   byte-identical through every migration below. Also fixed the scaffolding
   test's duplicate `test_support.h` include (the `ScopedLogCapture`
   redefinition -- it comes from the PCH).
2. **Schema + generated docs** -- `libs/draxul-config/src/config_schema.cpp`
   (public API in `include/draxul/config_schema.h`): one descriptor table of 51
   fields + 4 sections. Query surface, `register_module_table` extension hook,
   `render_config_docs_markdown` -> `docs/config-keys.generated.md`.
   `tests/config_schema_tests.cpp`: completeness, sections, hook, docs-freshness.
3. **Core-merge ownership** from `for_each_core_top_level_key`
   (`config_document.cpp`; deleted `kCoreTopLevelKeys`).
4. **Top-level unknown-key warning** from `is_core_top_level_key`
   (deleted `kKnownTopLevelKeys`).
5. **Serialization** fully schema-driven -- `serialize_fields` in the driver;
   `AppConfig::serialize()` only keeps the compound `[keybindings]` writer.
   Deleted `clamp_window_dimension` and `color_to_hex`.
6. **[chrome] key inventory** from `config_schema::fields()` (deleted
   `kKnownChromeKeys` -- the last hand-synchronized key list).
7. **Wrong-type pass** from `config_schema::check_types` (deleted ~90 lines of
   hand-written `check_*` lambdas).

Descriptor shape (`ConfigFieldDesc`): section, key, `ValueKind`, `FieldRef`
(a `const T& (*)(const AppConfig&)` accessor -- also the source of the default),
parse `RangeRule`, serialize `RangeRule`, min/max, warn-out-of-range, `EmitRule`,
description. The driver lives in `src/config_schema_driver.h` +
`config_schema.cpp`.

### Remaining (next agent) -- value parse + range validation

`config_from_toml` still parses values by hand (`parse_window_dimension`,
`parse_font_size`, `parse_atlas_size`, `parse_scrollback_lines`, the
scroll_speed / palette / focus / toast blocks, chord/weather/font paths/fallback,
and `apply_terminal_overrides` / `apply_chrome_overrides` /
`apply_markdown_overrides`). The descriptors' `parse_range` column is populated
but **not yet consumed**; `config_schema_driver.h` declares
`parse_top_level_fields`, `parse_section_fields`, and `warn_unknown_keys` but
they are **not implemented yet** (declared-not-defined is fine -- nothing calls
them). To finish:

- Implement `parse_top_level_fields` (apply `parse_range` per `ValueKind`, write
  through the accessor via `const_cast` as the header intends) and
  `parse_section_fields` (markdown clamp; terminal `HexString` validate-and-store
  with a warn; chrome `ColorHex` parse-to-`Color` with a warn).
- Apply the one cross-field rule **between** the two passes:
  `config.markdown.font_size = config.font_size` (markdown font size follows the
  global font size unless `[markdown].font_size` is set).
- Keep hand-written: `apply_gui_keybindings` (compound) and the duplicate-binding
  warning.
- **Watch-outs / semantics to preserve** (tests are substring-based on the key
  name via `has_warn_log`/`contains_message`, so exact WARN text is not pinned,
  but behavior is):
  - `scroll_speed` out of range -> falls back to **1.0** with a WARN
    (`corrupt_config_recovery_tests.cpp`).
  - `scrollback_lines` / terminal `selection_max_cells` / `paste_confirm_lines`
    out of range -> keep default + WARN (`RangeRule::UseDefault`,
    `warn_out_of_range = true`).
  - window dims: `UseDefault`, **no** warn.
  - `palette_bg_alpha` currently parses **double-only** (an integer literal is
    silently ignored); the `Float` `ValueKind` accepts integer-or-float, so
    migrating it *widens* acceptance. No test covers the integer case, but call
    this out or special-case it if strict parity is required.
  - `atlas_size` = clamp then floor-to-power-of-two (`RangeRule::PowerOfTwo`).
- Then delete the hand-written parse helpers and flip `config_from_toml` to the
  driver; the parity goldens + `[config]` suite must stay green.

### Known transient artifact

`floor_to_power_of_two` exists in both `app_config_io.cpp` (atlas *parse*) and
`config_schema.cpp` (atlas *serialize*). It collapses to one copy when parsing
moves onto the driver.

### Validation performed

- `[parity]` byte-exact and `[config]` (136 cases) green at every commit.
- Full `ctest` 11/11 and `do.py smoke` green before every group commit.
- Modules **off**: `build-nomods` configured with
  `DRAXUL_ENABLE_{MEGACITY,SATVIEW,SCOREVIEW}=OFF`; `draxul-config` (the whole
  change surface -- it has no module dependency) builds clean.

## Dependencies and parallelism

Depends on item 07. One config owner should control the schema; module-specific migrations can be delegated after the extension API is stable.

<model>GPT-5 Codex</model>
