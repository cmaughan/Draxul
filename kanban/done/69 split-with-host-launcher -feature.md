# Split with host launcher

**Type:** feature
**Disposition:** Implemented

The command palette expands vertical split, horizontal split, and new-tab actions
for every provider supported by the launch context plus discovered native plugins.
GUI action handling routes generic launch targets through authoritative topology
mutation, and `PaneManager` rolls back a new leaf when host initialization fails.
Provider visibility is covered by `tests/host_provider_availability_tests.cpp`.

Richer typed source/config completion remains part of
`kanban/ice-box/58 command-palette-structured-query -feature.md`.
