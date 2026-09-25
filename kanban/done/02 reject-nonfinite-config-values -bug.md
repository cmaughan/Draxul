# Reject non-finite numeric configuration before resource initialization
**Severity:** CRITICAL  
**Source:** Codex #3; `libs/draxul-config/src/config_schema.cpp:553`.

`font_size = nan` survives range handling and reaches undefined integer conversion in font initialization, including through checked reload.

**Investigation**

- [x] Trace float schema fields through startup, checked reload, and downstream integer conversions.

**Fix strategy**

- [x] Reject or safely default non-finite values before range handling, with explicit checked-reload diagnostics.
- [x] Validate numeric inputs defensively at the font initialization boundary.

**Acceptance criteria**

- [x] NaN and infinities never reach native font-size conversions; failed reload retains the prior valid configuration.
- [x] Verify finite boundary behavior, then run core aggregate tests and a same-cache smoke.

**Implementation:** The schema now checks all seven floating-point TOML fields for
`nan`/`inf` before applying range rules. Tolerant startup parsing uses field defaults;
checked parsing reports the dotted key and source line so a reload fails before the
candidate configuration is published. Serialization also defaults invalid in-memory
float values. The font manager validates point size and display PPI before FreeType
casts; text-service resize rejects non-finite point sizes before clamping, and rich
text style normalization avoids NaN keys in its style map.

**Validation added:** Config tests cover every float field, checked in-memory and
file parsing diagnostics, and the 6/72 point finite boundaries. Font tests exercise
non-finite initialization/PPI/resize and verify the previous size survives failed
resizes. The app reload smoke test checks that a `font_size = nan` reload does not
notify hosts before a subsequent valid reload.

**Windows focused gate (2026-09-24):** `py do.py test debug --target draxul-test-app
--catch "[config]"` passed 95 cases and 644 assertions (0.59 s test time);
`py do.py test debug --target draxul-test-core --catch "[font][config]"`
passed 1 case and 10 assertions (0.05 s test time). Build completed; core
recompiled 36 targets after concurrent changes.

**Windows shared gate (2026-09-24):** Debug core aggregate passed 25/25 CTest
entries. A same-cache single-pane `--smoke-test --host nvim` passed in 8.6
seconds, and the rebuilt Release app passed `--smoke-test`. The default
`py do.py smoke --skip-build` timed out at 30 seconds while loading the
existing nine-pane shared Session; the same Debug binary finished that
startup with a 90-second bound. macOS CI did not run in that pass.

[Build workflow run 35989867854](https://github.com/cmaughan/Draxul/actions/runs/35989867854)
stopped in both hosted jobs during checkout because the workflow token cannot
read the private `draxul-pcbview` submodule. macOS config/font execution did not occur.

2026-09-25 follow-up: The current Windows Debug core + all-products aggregate
passed 49/49 CTest entries in 238.19 seconds. The standard 30-second same-cache
smoke again timed out while restoring the existing nine-pane Session; the
identical Debug executable and wrapper environment passed under a 90-second
bound in roughly 45 seconds. macOS config/font execution had not occurred at
that stage.

**Platform-scope decision (2026-09-25):** Non-finite rejection lives in the
shared schema and FreeType/HarfBuzz initialization boundary, with no changed
OS-specific implementation. Windows focused, aggregate, and startup coverage
closes this card; a macOS rerun is not required for this shared-code fix.
