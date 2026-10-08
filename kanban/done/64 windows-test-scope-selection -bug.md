# Repair the Windows test-scope integration check

**Summary:** Make the test-scope check reliably find the tests it creates so it can verify that core and plugin test selections are correct.

**Priority:** P2
**Source:** `tests/test_scope_integration.py:36`, discovered while validating [62 shared-zlib-install-exports -bug.md](../done/62%20shared-zlib-install-exports%20-bug.md).

- [x] Confirm the failure repeats outside parallel aggregate execution.
- [x] Inspect the temporary fixture's generated CTest metadata, generator/configuration and selector arguments to establish why selection is empty.
- [x] Fix the fixture or production selector according to the observed cause; retain actual CMake/CTest integration.
- [x] Run the core aggregate, same-cache smoke and final Release startup.

Windows 2026-10-01: `draxul-test-scope-integration` fails at its first set comparison: `test-app-shell`, `test-host-api`, `test-server` and `script-integration` are all absent from `selected()`. It repeats in a serial retry (2.15 seconds). The real core/Rezonality aggregate selects 68 entries correctly. No test-selection implementation was changed by the build fix; root cause remains open.

## Diagnosis and fix — Windows, 2026-10-08

The fixture configured `tests/cmake/test_scopes` without `-G`, so on Windows CMake
4.4.3 chose the multi-config **Visual Studio 17 2022** generator. CTest 4.4.3 lists
no tests from a multi-config tree unless a configuration is named:
`ctest --show-only=json-v1` returned `"tests": []`, and with `--label-regex
^scope-(core)$` reported `Total Tests: 0`; adding `-C Debug` selected exactly
`test-app-shell`, `test-host-api`, `test-server` and `script-integration`. The
generated metadata and the `scope-*` labels were correct. The production
selector is also correct: `do.py` always passes `--build-config <config>` to
CTest, which is why the real aggregate selected its entries.

Fix: the fixture's `selected()` now passes `--build-config Debug`, mirroring
`do.py`, and keeps the real CMake configure plus CTest label intersection.
`py -m unittest tests.test_scope_integration`: 1 test passed in 2.51 s
(focused iteration, real configure + 10 CTest listings).

## Validation summary — Windows, 2026-10-08

- Configure/build: fresh Ninja Debug cache after fast-forwarding 573 commits
  (1298 steps), then incremental rebuilds per fix; Release cache rebuilt
  (`py do.py build release`).
- Core + products aggregate (`py do.py test debug --products`): 85/90 CTest
  entries passed in 533 s. The five failures were unrelated to these changes:
  `personal_agent_tests.cpp:488` `remove_all` (deterministic, also failed in
  the earlier gate, tracked on `kanban/pending/67 personal-assistant-host -feature.md`);
  Rezonality native shard failing only on the stale `W:\p4` implicit-layer
  loader manifest (421/422); `draxul-render-flashcards-queue` aborted because
  the desktop pointer moved / capture was unavailable while the machine was in
  use; and two Windows encoding test bugs fixed in the same session and rerun
  green (`personal_assistant_host_tests.cpp` CJK `u8` literal, 5/5 cases;
  `draxul-rezonality-agent-layout` UTF-8 decoding, passed 5.9 s).
- Same-cache Debug smoke: `py do.py smoke debug --skip-build` passed (about
  6 s, isolated runtime; see `kanban/pending/65 windows-validation-timing -test.md`).
- Final Release startup: `py do.py smoke release --skip-build` passed in 4.8 s
  from the Release cache (isolated runtime, owned server shut down).
- Remote CI not run; macOS/Metal paths rely on normal cross-platform CI.
