# Delete archived Kanban cards with capital D

**Type:** feature
**Priority:** 91

## Work

- [x] Map capital `D` to a non-repeating delete command.
- [x] Delete the selected Markdown card only when its lane is `done` or
      `ice-box`, including cards owned by aggregated submodule boards.
- [x] Keep pending and other lanes intact and explain the restriction with a
      toast.
- [x] Remove the deleted card from persisted board ordering and move selection
      and any pinned preview to a surviving card.
- [x] Document the shortcut and cover navigation, storage, and host behavior.

## Acceptance criteria

- [x] One capital `D` keypress deletes exactly one selected card from `done` or
      `ice-box`.
- [x] Holding `D` cannot cascade across multiple cards before key release.
- [x] The shortcut cannot delete cards from `pending` or custom lanes.
- [x] Kanban focused tests, aggregate tests, and same-cache smoke pass.

## Validation evidence

- Focused compilation built `draxul-test-kanban-core` and
  `draxul-test-kanban-host` in the existing Debug cache.
- Kanban core passed 278 assertions in 37 cases; the deletion host tag passed
  28 assertions in 2 cases. An initial full host run repeated two known
  FSEvents-dependent failures inside the filesystem sandbox; the host-permission
  aggregate passed those same tests.
- `python3 do.py test debug` passed 25/25 registered core entries in 41.36 s,
  including both Kanban shards and the native file-monitor shard.
- `python3 do.py smoke debug --skip-build` passed from the same cache on Apple
  M5. No render comparison was required because the feature does not change
  board layout or renderer output.
