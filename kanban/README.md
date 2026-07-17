# Kanban — work items

The project's only work-item tracker. (Design docs and research notes live in
[`plans/`](../plans/README.md); they are not work items.)

## Board

| Directory | Purpose |
|-----------|---------|
| `pending/` | Active items — in scope for the current or next work session |
| `ice-box/` | Deferred items — good ideas, not yet scheduled |
| `done/` | Completed items — kept for reference |

## File naming

```
<number> <slug> -<type>.md
```

- **number** — priority/sequence. Unique within `pending/`, but numbers are reused across
  planning waves and can collide between directories.
- **slug** — hyphenated short description.
- **type** — one of `bug`, `test`, `feature`, `refactor`.

## Cross-reference rule

Always reference an item by its **full filename**, never by number alone, because numbers
are reused across waves and directories:

```
# Good
See kanban/ice-box/20 url-detection-click -feature.md

# Bad — ambiguous
See item 20
```

This rule is why colliding numbers are tolerated rather than renumbered: a full filename is
unambiguous regardless. Known collisions in `ice-box/` (as of 2026-03-22): number 19
(`guicursor-full-support`, `per-monitor-dpi-font-scaling`) and number 22
(`agent-scripts-deduplication-refactor`).

## Working an item

- Tick completed entries with Markdown task ticks (`- [x]`) **in the same turn** as the
  work; leave incomplete follow-ups unchecked so progress stays visible in the file.
- When an item is fully complete, move it from `pending/` to `done/` in the same turn and
  fix any links that pointed at the old location.
- Before creating a new item, check `docs/features.md` — the capability may already exist.

## Board sync

`python do.py syncboard` pushes `pending/` (as **Backlog**) and `ice-box/` (as **IceBox**)
to the GitHub project board (project #1, "Draxul"). It is idempotent.
