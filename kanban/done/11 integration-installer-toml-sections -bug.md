# Preserve valid TOML while enabling integration hooks
**Severity:** HIGH
**Source:** Codex #12; `libs/draxul-agent-integration/src/agent_integration.cpp:322`.

A valid `[features] # comment` header is missed, causing installation to append a duplicate table and invalidate the configuration.

**Investigation**

- [x] Examine section recognition with comments, whitespace, quoted keys, and neighboring tables.

**Fix strategy**

- [x] Transform TOML structure correctly while preserving unrelated configuration.
- [x] Validate transformed content before atomic publication.

**Acceptance criteria**

- [x] Installation handles commented headers without duplicate tables or misplaced assignments and remains idempotent.
- [x] Invalid transformations never replace the original file; run core aggregate tests and same-cache smoke.

**Implementation and validation notes (2026-09-25):** The old line scanner
recognized only a literal `[features]` line, and status inspection repeated
the same mistake. Both now read the parsed TOML structure. For normal section
headers and existing boolean values, the installer patches only the necessary
line, retaining comments and neighboring tables; inline/implicit tables use a
semantic TOML rewrite. It checks the transformed document and `hooks = true`
before publishing, and refuses malformed or wrong-type input without touching
`config.toml`. Regression tests cover quoted/commented section headers, dotted
assignment, neighboring tables, idempotence, and invalid originals.

Final Windows validation: `py do.py test debug --products` passed 49/49 CTest
entries in 238.19 seconds, including the integration installer cases. The
standard fixed-30-second smoke timed out loading the existing nine-pane
Session; the same-cache smoke in the exact wrapper environment with a
90-second bound passed in approximately 45 seconds.
