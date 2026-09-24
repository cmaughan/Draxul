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
- [ ] Validate the configuration and font paths on macOS CI.

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
startup with a 90-second bound. macOS CI is still pending.
