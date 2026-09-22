# WI 126 — Aggregate Recursive Submodule Kanban Boards

**Type:** Feature  
**Severity:** Medium (tracker visibility)

---

## Problem

The native Kanban host only shows the selected repository's `kanban/` folder, so
work in initialized submodules is invisible unless each board is opened separately.

## Acceptance Criteria

- [x] Load the current repository board plus boards from initialized recursive Git submodules by default.
- [x] Preserve source identity when different boards contain cards with the same filename.
- [x] Group and label aggregate cards by source board.
- [x] Provide a fast keyboard filter for all, root, and individual submodule boards.
- [x] Keep reorder and cross-column moves inside the card's owning board.
- [x] Persist ordering to the owning board's `.draxul-kanban.toml` only.
- [x] Skip uninitialized submodules and retain healthy boards when one sub-board cannot be scanned.
- [x] Cover discovery, failure handling, mutations, persistence, navigation, and host filtering with tests.
- [x] Document the aggregate behavior and key binding.

## Validation

- [x] Focused Kanban core tests pass.
- [x] Focused Kanban host tests pass.
- [ ] Aggregate repository test gate passes.
- [ ] Same-cache smoke passes.

The Debug aggregate built successfully and its Kanban core/host entries passed.
The complete run remains blocked by unrelated Windows environment failures: a
child Neovim process intermittently received an empty `TERM`, and separate runs
hit `Temp` filesystem `unknown error` failures. The same-cache smoke was also
terminated by its 30-second startup timeout.
