# Plugin storage private-owner extraction

This slice implements `kanban/done/94 plugin-storage-private-owner
-refactor.md` and closes the remaining deterministic investigation in
`kanban/done/58 plugin-storage-error-preservation -bug.md`.

## Ownership

The private `PluginStorage` collaborator owns service-path layout, storage
scope/key validation, JSON file limits and parsing, NUL-inclusive buffer
contracts, temporary-file replacement, and reload overlays. It remains an
implementation detail of `draxul-plugin-host`; its header lives under `src/`
and is not installed or exposed by a public target include directory.

`PluginHost` continues to own callback token validity, generation and main
thread checks, ABI service-table callbacks, plugin instance lifecycle, reload
activation, and the cohort-facing `finalize_reload_storage()` operation. A
storage overlay never decides whether a candidate activates.

Overlay commit preserves the existing partial-publication behavior. Entries
are applied in the overlay map's iteration order and commit stops at the first
failure. Earlier entries remain published, later entries are discarded, and
no multi-file rollback is attempted.

## Integration

1. `src/plugin_storage.cpp` is part of `draxul-plugin-host`, while `src` remains private.
2. `PluginHost`'s storage paths, root override, overlay flag, and map are replaced
   with one `PluginStorage` member initialized from the existing constructor
   override.
3. The collaborator is initialized when the plugin identity and module directory
   are known. Path queries and storage operations delegate only after the
   existing callback and main-thread checks.
4. Reload overlay mutations use `begin_overlay()`,
   `discard_overlay()`, and `commit_overlay()`. Keep App's existing call to
   `PluginHost::finalize_reload_storage()` unchanged.
5. The duplicated filesystem/JSON helpers and now-unused includes were removed
   from `plugin_host.cpp`.

## Direct and integration coverage

Direct cases live in the existing `tests/plugin_manager_tests.cpp` source, so no
new root `*_tests.cpp` target classification is needed. Give only
`draxul-test-app` private access to `libs/draxul-host/src`; do not export the
header through `draxul-plugin-host`.

The direct cases verify:

- override-root resource/config/data/cache/temporary layout, plugin and pane
  scope paths, path-query directory creation/failure, invalid keys, invalid
  scopes, and pane-scope unavailability;
- missing files, invalid persisted JSON, oversized persisted and proposed
  values, two-call reads, required sizes including NUL, and small buffers;
- overlay write visibility, tombstones, disk fallback, discard, successful
  commit, remove commit, and partial publication when a later entry fails;
- injected directory, open, write, flush, replace, read, and remove failures;
- for open/write/flush/replace failures, cleanup is attempted with a separate
  error object and a successful cleanup never changes the primary diagnostic
  to `Success`.

The existing loaded-module cases remain responsible for service discovery,
wrong-thread rejection, stale callbacks, successful reload commit, failed
candidate discard, and rollback integration.

## Validation

The focused `draxul-plugin-host` and `draxul-test-app` builds passed, followed
by 99 assertions in four direct storage cases. `python3 do.py test debug
--products` passed all 39 shards in 113.73 seconds, and `python3 do.py smoke
--skip-build` passed from the same cache. Source review confirms `MoveFileExW`
still uses `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`; the macOS run
exercised POSIX rename replacement and UTF-8 path round-trips.
