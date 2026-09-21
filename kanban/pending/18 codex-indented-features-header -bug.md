# Locate indented features headers without invalid iterators

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`app/agent_integration.cpp:420` searches untrimmed lines after detecting a trimmed `[features]` header. With `  [features]` and no hooks key, `:421` inserts through `end() + 1`, invoking undefined behavior.

**Investigation**

- [ ] Cover indented headers with missing and existing hooks settings.

**Fix strategy**

- [ ] Record the matching header index during the initial scan.
- [ ] Insert only through a validated position while preserving unrelated settings.

**Acceptance criteria**

- [ ] Installation safely enables hooks for indented and unindented headers.
- [ ] Repeated installation remains idempotent and preserves valid TOML.
