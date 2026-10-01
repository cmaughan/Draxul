# Repair the Windows test-scope integration check

**Summary:** Make the test-scope check reliably find the tests it creates so it can verify that core and plugin test selections are correct.

**Priority:** P2
**Source:** `tests/test_scope_integration.py:36`, discovered while validating [62 shared-zlib-install-exports -bug.md](../done/62%20shared-zlib-install-exports%20-bug.md).

- [x] Confirm the failure repeats outside parallel aggregate execution.
- [ ] Inspect the temporary fixture's generated CTest metadata, generator/configuration and selector arguments to establish why selection is empty.
- [ ] Fix the fixture or production selector according to the observed cause; retain actual CMake/CTest integration.
- [ ] Run the core aggregate, same-cache smoke and final Release startup.

Windows 2026-10-01: `draxul-test-scope-integration` fails at its first set comparison: `test-app-shell`, `test-host-api`, `test-server` and `script-integration` are all absent from `selected()`. It repeats in a serial retry (2.15 seconds). The real core/Rezonality aggregate selects 68 entries correctly. No test-selection implementation was changed by the build fix; root cause remains open.
