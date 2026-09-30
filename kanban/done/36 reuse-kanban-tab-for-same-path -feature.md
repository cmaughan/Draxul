# Reuse a Kanban tab for the same board path

**Priority:** P2  
**Raised by:** user, 2026-09-30

## Goal

Requesting a Kanban board already open in the shared Session should select
its existing tab, including when the request uses an equivalent relative or
absolute path. It should not add another Kanban tab.

## Tasks

- [x] Resolve requested and existing Kanban paths consistently at the server.
- [x] Return the existing tab ID for a matching board, across Spaces.
- [x] Supply the client working directory for command-palette Kanban tabs.
- [x] Cover repeated and distinct paths, and existing-tab selection.
- [x] Update feature documentation and run the Debug aggregate plus same-cache smoke.
- [x] Confirm Release startup with a smoke run.

## Notes

The server owns shared tab creation, so path reuse belongs in its CreateTab
transaction. The existing client activation path selects the tab named by the
command result, whether newly created or reused.

The server compares normalized board roots (and existing directory identity),
including the default `<working directory>/kanban` path. Matching is limited
to Kanban owner panes; other host types and distinct board paths still create
new tabs. The app smoke test confirms a returned existing ID switches away
from an active shell tab without adding a host or tab.

Debug core aggregate: 55/55 CTest entries passed in 33.80s. A focused app
smoke regression passed 26 assertions in one case after replacing a brittle
50ms per-poll timing assertion with the existing overall deadline. Same-cache
Debug startup smoke passed with Metal. Release app build and `--smoke-test`
also passed with Metal. No Kanban tab
render snapshot exists; the app smoke test covers the tab-selection behavior.
