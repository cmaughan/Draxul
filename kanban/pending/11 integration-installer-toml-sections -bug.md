# Preserve valid TOML while enabling integration hooks
**Severity:** HIGH  
**Source:** Codex #12; `libs/draxul-agent-integration/src/agent_integration.cpp:322`.

A valid `[features] # comment` header is missed, causing installation to append a duplicate table and invalidate the configuration.

**Investigation**

- [ ] Examine section recognition with comments, whitespace, quoted keys, and neighboring tables.

**Fix strategy**

- [ ] Transform TOML structure correctly while preserving unrelated configuration.
- [ ] Validate transformed content before atomic publication.

**Acceptance criteria**

- [ ] Installation handles commented headers without duplicate tables or misplaced assignments and remains idempotent.
- [ ] Invalid transformations never replace the original file; run core aggregate tests and same-cache smoke.
