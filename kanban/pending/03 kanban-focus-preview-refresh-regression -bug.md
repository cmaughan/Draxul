# Investigate redundant Kanban preview updates after refocus

**Source:** Core Release aggregate validation, 2026-09-29, while validating
`kanban/done/02 macos-server-menu-bar-item-disappears -bug.md`.

The existing test `kanban host cancels held navigation and preview updates
when focus moves away` fails at `tests/kanban_host_tests.cpp:631`: the preview
callback count increases from 2 to 3 after refocus and a 180 ms pump interval,
although the selected card has not changed. Navigation and selected preview
path assertions pass. No Kanban implementation or test code changed in the
menu-bar investigation.

Related completed work:
`kanban/done/12 kanban-focus-loss-repeat-state -bug.md` explicitly allows
a file-monitor reconciliation to refresh the same pinned preview after
refocus. The current count-equality assertion appears inconsistent with that
documented intent; first establish this event boundary before changing host
behavior or weakening the background-navigation assertions.

- [x] Reproduce the failure in the core aggregate and an independent shard rerun.
- [ ] Determine whether a deferred file-monitor refresh is legitimate or whether
  the host issues a redundant preview update after focus returns.
- [ ] Fix the behavior or synchronize the test to the real event boundary; retain
  coverage that a held navigation key cannot continue after focus loss.
- [ ] Run the Kanban host aggregate and a same-cache startup smoke.

**Evidence:** `python3 do.py test release` passed 24/25 CTest entries; this
shard passed 19/20 cases and 160/161 assertions. Rerunning
`ctest --test-dir build --build-config Release -R '^draxul-test-kanban-host-shard-0$' --output-on-failure`
reproduced the identical failure. Catch2 seeds were `3098772745` and
`3229628080`; the shard took 2.76 s and 2.40 s respectively. The aggregate
took 32.83 s, and the same-cache Release smoke passed. Investigate separately
from the macOS helper bundle icon/placement work.
