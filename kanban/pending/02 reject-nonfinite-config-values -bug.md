# Reject non-finite numeric configuration before resource initialization
**Severity:** CRITICAL  
**Source:** Codex #3; `libs/draxul-config/src/config_schema.cpp:553`.

`font_size = nan` survives range handling and reaches undefined integer conversion in font initialization, including through checked reload.

**Investigation**

- [ ] Trace float schema fields through startup, checked reload, and downstream integer conversions.

**Fix strategy**

- [ ] Reject or safely default non-finite values before range handling, with explicit checked-reload diagnostics.
- [ ] Validate numeric inputs defensively at the font initialization boundary.

**Acceptance criteria**

- [ ] NaN and infinities never reach native font-size conversions; failed reload retains the prior valid configuration.
- [ ] Verify finite boundary behavior, then run core aggregate tests and same-cache smoke.
