# Kanban preview cannot reopen after closing

**Priority:** P1  
**Raised by:** user, 2026-09-30

## Reproduction

In a shared Kanban tab, press `p` to create the Markdown preview, press `p`
to close it, then press `p` again. The close is asynchronous, so the old pane
can still appear visible when the third keypress arrives. Kanban treated that
stale visibility as another request to close instead of a request to reopen.
The live server also had a Tab 7 companion preview pointing at a card that had
moved from `kanban/pending/` to `kanban/done/`. The missing Markdown source
caused the entire projected tab to fail initialization.

## Tasks

- [x] Trace Kanban preview intent through the server-backed split/close path.
- [x] Keep user-requested preview state independent from a stale projected pane.
- [x] Reopen after the close projection lands; avoid refreshes during close.
- [x] Add a delayed-close regression test and a normal open/close/reopen test.
- [x] Keep a projected companion preview available when its card path has moved,
      so the Kanban owner tab can load and retarget it.
- [x] Add a missing-source companion regression test.
- [x] Run Debug aggregate tests and same-cache smoke; verify the live tab topology.

## Notes

The change is in the Kanban host; the existing App callback remains the
authority for whether the server's split has appeared or disappeared. The
host defers a reopen until the previous companion pane is gone, then opens the
currently selected card. This avoids duplicate close commands and stale
selection refreshes while the close is still in flight.

Read-only server inspection confirmed that the seventh tab was `tab-1-96` and
its companion `pane-97` pointed at a missing Pending path; the card now exists
in Done. Another Kanban tab had the same condition (`pane-79`). With no UI
attached, both stale client-local preview panes were closed through the
server's exact pane IDs. Rechecking each tab showed its Kanban owner pane
intact (`pane-94` and `pane-73`). No terminal pane was affected.

Validation on macOS: focused Kanban input tests passed (113 assertions in 14
cases), focused Markdown companion tests passed (19 assertions in 2 cases),
and the final Debug aggregate passed all 55 CTest entries. The same-cache
`python3 do.py smoke debug --skip-build` passed with the Metal renderer. The
two affected live tabs were rechecked after closing the stale companion panes;
each retained its Kanban owner pane and no missing-source preview.
