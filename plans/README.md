# Plans

## Directory layout

| Directory | Purpose |
|-----------|---------|
| `work-items/` | Active items — in scope for the current or next work session |
| `work-items-icebox/` | Deferred items — good ideas, not yet scheduled |
| `work-items-complete/` | Done items — kept for reference |
| `prompts/` | Saved prompt templates and consensus prompts |
| `reviews/` | Agent review outputs |

## File naming convention

```
<number> <slug> -<type>.md
```

- **number** — sequential within the wave it was created; unique within `work-items/`
  but numbers can collide across waves and between `icebox/` and `complete/`.
  Always use the full filename (not just the number) in cross-references.
- **slug** — hyphenated short description
- **type** — one of `bug`, `test`, `refactor`, `feature`

## Cross-reference rule

Always reference items by their **full filename**, not their number alone, because
numbers are reused across planning waves:

```
# Good
See plans/work-items-icebox/20 url-detection-click -feature.md

# Bad — ambiguous
See item 20
```

## Known number collisions in icebox (as of 2026-03-22)

| Number | Files |
|--------|-------|
| 19 | `guicursor-full-support`, `per-monitor-dpi-font-scaling` |
| 22 | `agent-scripts-deduplication-refactor` *(url-detection duplicate removed)* |
| 34 | `background-transparency`, `configurable-scrollback-capacity` |

These will be renumbered when the items are promoted to active `work-items/`.
