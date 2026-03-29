# 32 citydb-schema-migration -test

*Filed by: claude-sonnet-4-6 — 2026-03-29*
*Source: review-latest.gpt.md [P]*

## Problem

The MegaCity SQLite database uses `PRAGMA user_version` to track schema version.  Two upgrade
scenarios are untested:

1. **Forward migration**: A DB file at an older `user_version` should be automatically
   migrated to the current schema when the app starts.  If migration fails, the app should
   log an error and either recreate the DB or refuse to start with a clear message.

2. **Future/unknown schema version**: If a DB file has a `user_version` higher than the
   current code understands (e.g., from a newer Draxul binary), the app must not silently
   corrupt it.  Expected behaviour: error + refuse to open (or open read-only).

3. **`megacity_db_path()` resolution**: Tests should verify that the DB path resolves
   correctly for both the repo-root case (development) and the app-support directory case
   (installed app).

Without migration tests, a schema change in a commit can silently corrupt existing user DBs
on upgrade.

## Acceptance Criteria

- [ ] A test creates a DB at schema version N-1, opens it with current code, and verifies
      the schema is upgraded to version N without data loss.
- [ ] A test creates a DB at an unknown future schema version and verifies the code refuses
      to open it (or logs an appropriate error).
- [ ] A test verifies `megacity_db_path()` returns the app-support path when no repo root
      is detected, and the repo-root path when `DRAXUL_REPO_ROOT` or equivalent is set.
- [ ] All tests pass under `ctest`.

## Implementation Plan

1. Read `libs/draxul-megacity/src/` to find the DB open/migration code and
   `megacity_db_path()` implementation.
2. Identify the schema version constants and migration steps.
3. Use a temporary file path (`std::filesystem::temp_directory_path()`) for test DBs to
   avoid polluting the user's app-support directory.
4. Write the three test cases using the existing megacity test infrastructure.
5. Run `ctest -R citydb`.

## Files Likely Touched

- `tests/citydb_migration_tests.cpp` (new)
- `tests/CMakeLists.txt`

## Interdependencies

- **WI 41** (`cmake-configure-depends`) should land first.
- Complements active `10 citydb-reconcile-robustness -test` (different failure modes).
