# Remove legacy version support

**Type:** refactor
**Priority:** P1 / sequence 133
**Raised by:** user, 2026-09-23

## Goal

Draxul has one user and no released compatibility contract yet. Keep persisted data,
plugin packages, protocol payloads, and client transports on the current format so
obsolete fallback paths cannot hide defects or expand the supported state space.

## Implementation

- [x] Record the no-legacy-support policy in the repository agent guidance.
- [x] Remove the per-pane Session transport fallback and its tests.
- [x] Require the current persisted Session schema; remove v1-v3 migrations and tests.
- [x] Require generation-pointer plugin packages; remove top-level manifest fallback.
- [x] Require current protocol payload fields and replace permissive legacy tests.
- [x] Require the exact current server and plugin SDK protocol versions.
- [x] Remove historical config parsing exceptions and their tests.
- [x] Remove restore-time generated-name and identity compatibility repairs.
- [x] Require stable pane identities in current snapshots and projections.
- [x] Require explicit server terminal and process-state identities.
- [x] Remove transitional Nvim and rich-text compatibility adapters.
- [x] Update documentation and stale comments that promise these compatibility paths.

## Validation

- [x] Run the scope-appropriate Debug aggregate tests.
- [x] Run a same-cache Debug startup smoke.

## Notes

- Current protocol version/authentication checks remain required. They reject stale or
  incompatible peers and define the one supported format; they are not compatibility
  fallbacks.
- Do not preserve old fixtures merely to test removed behavior. Tests should assert that
  obsolete formats are rejected where a clear error is part of the current contract.
- Session transport now supports the current event stream and aggregate Session poll
  paths only. Per-pane workers and their load baselines were removed.
- Session state accepts version 4 only and requires globally unique pane identities.
  The v1 fixtures and v1-v3 migration code were deleted.
- Plugin discovery requires `current.json` plus a generation manifest, server clients
  require the exact protocol major/minor, and the SDK CMake package requires an exact
  version.
- Current JSON payloads require their authentication, status, topology, terminal ID,
  and process-state fields. Tests now construct those identities explicitly.
- Removed config exceptions for retired fields, restore-time name/identity repair,
  Nvim value factories, rich-text atlas adapters, and their compatibility tests.
- Validation: `python3 do.py test debug` passed all 25 CTest entries on macOS Debug.
- Smoke: `python3 do.py smoke debug --skip-build` passed with the Metal renderer.
