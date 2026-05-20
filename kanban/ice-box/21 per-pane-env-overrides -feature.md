# 21 per-pane-env-overrides -feature

**Priority:** LOW
**Type:** Feature (developer ergonomics, config)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

There is currently no way to inject environment variables (e.g., `TERM`, `COLORTERM`, `DRAXUL_VERSION`) into child processes from `config.toml`. Users who need `COLORTERM=truecolor` set for proper color support, or who want to identify Draxul to shell scripts, must configure this in their shell profile rather than centrally in Draxul's config.

---

## Implementation Plan

- [ ] Read `app/app_config.h` to understand the current per-host or global config sections.
- [ ] Read `libs/draxul-host/src/shell_host_unix.cpp` (and Windows equivalent) to understand how environment is currently set for child processes.
- [ ] Add a `[hosts.shell]` (or `[hosts.nvim]`) config table with an `env` sub-table:
  ```toml
  [hosts.shell]
  env = { COLORTERM = "truecolor", DRAXUL = "1" }

  [hosts.nvim]
  env = { NVIM_GUI = "draxul" }
  ```
- [ ] Add `HostEnvConfig` (map of string → string) to `AppConfig`.
- [ ] Parse the `env` sub-table during TOML load.
- [ ] Plumb the env map into `ShellHost` and `NvimHost` constructors/`initialize()` via the existing `Deps` pattern.
- [ ] In the process spawn paths (`fork()`/`exec()` on Unix, `CreateProcess` on Windows): apply the env overrides on top of the inherited environment (do not replace the entire environment).
- [ ] Build and run smoke test.

---

## Acceptance

- A `COLORTERM = "truecolor"` entry in `[hosts.shell]` env is visible in the spawned shell via `echo $COLORTERM`.
- Env entries in `[hosts.nvim]` are visible to the embedded Neovim process.
- No env override does not break the default environment.

---

## Interdependencies

- `12-refactor` (app_config monolith split) — not a hard dependency but makes adding config sections cleaner.
- No upstream blockers.

---

*claude-sonnet-4-6*
