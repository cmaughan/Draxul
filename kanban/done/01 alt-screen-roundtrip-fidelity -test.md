# Alternate-screen resize round-trip fidelity

**Type:** test
**Disposition:** Covered

`tests/terminal_semantic_replay_tests.cpp` enters alternate screen, resizes the
terminal, snapshots the alternate state, exits, and verifies restoration of the
main-screen content and digest. Terminal VT and scrollback suites provide the
lower-level alternate-screen behavior coverage.
