# Test: CityDatabase Reconcile Robustness

**Type:** test
**Priority:** 10
**Source:** Gemini review

## Problem

`CityDatabase` has essentially one focused reconcile test. The following scenarios are untested:

1. **Idempotent reconcile**: applying the same snapshot twice should not change the DB state.
2. **Deleted symbol**: a symbol present in snapshot A but absent in snapshot B should be removed from the DB after reconciling B.
3. **Renamed symbol**: old entry removed, new entry with new name added.
4. **DB reopen**: close the DB, reopen the same file, verify data is intact.
5. **Move/close semantics**: move the DB object, ensure the original handle is not double-closed.

Without these tests, silent regressions in the reconcile logic will produce wrong city layout data with no diagnostic.

**Note:** Do alongside `09 codebasescanner-lifecycle -test` and `12 megacity-degraded-init -test` — share fixture setup.

## Investigation steps

- [ ] Read `libs/draxul-megacity/src/` — find `CityDatabase` class and its `reconcile()` method.
- [ ] Read `tests/citydb_tests.cpp` — check current coverage.
- [ ] Understand the DB schema: what tables/columns represent symbols?
- [ ] Check how the DB file is opened and closed (SQLite path).

## Test design

Add to `tests/citydb_tests.cpp`.

### Idempotent reconcile

- [ ] Build a snapshot, reconcile it.
- [ ] Reconcile the same snapshot again.
- [ ] Assert: DB state is identical after both reconciles (row counts unchanged, no duplicates).

### Deleted symbol

- [ ] Reconcile snapshot A (contains symbol `Foo`).
- [ ] Reconcile snapshot B (does not contain `Foo`).
- [ ] Assert: `Foo` is no longer in the DB.

### Renamed symbol

- [ ] Reconcile snapshot A (`Foo` at location L1).
- [ ] Reconcile snapshot B (`Bar` at location L1, `Foo` absent).
- [ ] Assert: `Bar` present, `Foo` absent, no orphaned entries.

### DB reopen

- [ ] Open DB, reconcile a snapshot, close the DB.
- [ ] Reopen DB from the same file path.
- [ ] Assert: all symbols from the snapshot are still present.

### Move/close semantics

- [ ] Open DB, `std::move` the DB object to a new variable.
- [ ] Reconcile via the moved object.
- [ ] Let both go out of scope; assert no double-free (run under ASan).

## Acceptance criteria

- [ ] All five scenarios pass under ASan.
- [ ] Tests are part of `draxul-tests`.

## Interdependencies

- **`09 codebasescanner-lifecycle -test`** and **`12 megacity-degraded-init -test`**: share fixture, same agent pass.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
