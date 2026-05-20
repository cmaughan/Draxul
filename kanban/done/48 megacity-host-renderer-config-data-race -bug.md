# Bug: Data race on `renderer_config_` in MegaCityHost route worker

**Severity**: CRITICAL
**File**: `libs/draxul-megacity/src/megacity_host.cpp:354`
**Source**: review-bugs-consensus.md (C1)

## Description

`route_worker_loop()` reads `renderer_config_` at line 354 after releasing `route_mutex_` (lock dropped at line 342). The main thread writes `renderer_config_` at lines 655 and 679 without a lock. This is a C++ data race — concurrent unsynchronised access to a non-atomic object. Effects include torn config reads, corrupted route output, and undefined behaviour.

## Trigger Scenario

Change any Megacity rendering setting (focus mode, route-display options) while a route rebuild is in flight on the background worker thread.

## Investigation Steps

- [x] Locate all write sites for `renderer_config_` in `megacity_host.cpp`
- [x] Confirm none of those writes hold `route_mutex_`
- [x] Check if `RouteBuildRequest` already carries a config field that could be reused

## Fix Strategy

- [x] Add a `MegaCityCodeConfig config` field to `RouteBuildRequest`
- [x] When enqueuing a new request, copy `renderer_config_` into `request.config` under `route_mutex_`
- [x] In `route_worker_loop`, use `request.config` instead of `renderer_config_`
- [x] Remove any direct read of `renderer_config_` from the worker loop body

## Acceptance Criteria

- [x] `renderer_config_` is never read from `route_worker_loop` without going through the request snapshot
- [ ] Thread-sanitizer (TSan) reports no data race on `renderer_config_` under concurrent config changes
- [x] Megacity routing still produces correct results after the fix
