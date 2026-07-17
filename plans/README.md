# Plans

Design docs, research notes, and review output.

> **Work items do not live here.** They live in [`kanban/`](../kanban/README.md) —
> `kanban/pending/`, `kanban/ice-box/`, `kanban/done/`. The old `plans/work-items*`
> directories are gone; anything still pointing at them is stale.

## Directory layout

| Path | Purpose |
|------|---------|
| `<host>.md`, `<host>-<topic>.md` | Per-module design docs and master plans (e.g. `scoreview.md`, `scoreview-composer.md`, `satview-hdr-pipeline.md`) |
| `*-research.md` | Research/evidence notes backing a design (e.g. `scoreview-learning-research.md`, `music-notation-research.md`) |
| `design/` | Design assets and explorations |
| `prompts/` | Saved prompt templates and consensus prompts |
| `reviews/` | Agent review outputs |
| `superpowers/` | Agent tooling notes |

## Conventions

- A module's master plan is `<host>.md` (e.g. `scoreview.md`); focused sub-plans hang off
  it as `<host>-<topic>.md` and are linked from the master plan so they stay discoverable.
- Research notes state their evidence and confidence, and are linked from the design docs
  that depend on them — a design doc should never assert a finding its research note does
  not support.
- Plans describe *intent and rationale*; the work to get there is tracked as items in
  `kanban/`. When a plan's phase ships, tick it in the plan **and** move its kanban item
  to `kanban/done/`.
