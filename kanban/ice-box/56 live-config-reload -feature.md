# 56 Auto Config Reload

## Current State

Manual config reload is implemented. The `reload_config` action rereads `config.toml` and applies terminal font settings, Markdown font/margins, keybindings, scroll settings, ligatures, palette alpha, and host reload settings without restarting Draxul.

The remaining quality-of-life gap is automatic reload when the config file changes on disk.

## Goal

Watch the active user config file and call the existing `App::reload_config()` path after changes settle.

## Implementation Plan

- [ ] Confirm the active config path used by `AppOptions` and startup config loading.
- [ ] Add a small `ConfigWatcher` helper that polls file `mtime` periodically; avoid adding a new dependency.
- [ ] Debounce rapid saves so an editor writing through a temporary file does not trigger multiple reloads.
- [ ] Wire the watcher into the app loop only when user config loading is enabled.
- [ ] On change, call the existing `App::reload_config()` rather than duplicating config diff/apply logic.
- [ ] Surface reload success/failure via log and, if appropriate, a toast.
- [ ] Add unit tests for the watcher using a temp file.
- [ ] Add an app smoke test proving an on-disk config change is picked up without invoking the `reload_config` action.

## Acceptance Criteria

- Editing the active `config.toml` applies through the same behavior as `reload_config`.
- Multiple fast writes produce at most one reload after the debounce window.
- Invalid config leaves the previous runtime config intact and reports the failure.
- Manual `reload_config` continues to work.
