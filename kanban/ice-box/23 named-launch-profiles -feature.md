# Feature: Named Launch Profiles in config.toml

**Type:** feature
**Priority:** 23
**Source:** Gemini review

## Overview

Users want to launch Draxul with different host configurations without editing `config.toml` each time. Named launch profiles allow them to define multiple configurations — e.g. a "work nvim", a "personal shell", a "MegaCity code browser" — and select one at startup or via the command palette.

## Config format

```toml
[[profiles]]
name = "nvim-work"
host_type = "nvim"
args = ["--cmd", "set background=dark"]
cwd = "~/work/myproject"

[[profiles]]
name = "zsh"
host_type = "shell"
args = ["/bin/zsh", "--login"]
cwd = "~"

[[profiles]]
name = "megacity"
host_type = "megacity"
cwd = "~/work/myproject"
```

The first profile is used by default on startup; a `--profile <name>` CLI flag selects another.

## Implementation plan

### Phase 1: Config schema

- [ ] Read `libs/draxul-config/include/draxul/app_config_types.h` and `app_config_io.cpp` — understand how the current `host_type`, `nvim_args`, and `cwd` are represented.
- [ ] Add a `LaunchProfile` struct to `app_config_types.h`:
  ```cpp
  struct LaunchProfile {
      std::string name;
      std::string host_type; // "nvim", "shell", "megacity"
      std::vector<std::string> args;
      std::string cwd;
  };
  ```
- [ ] Add `std::vector<LaunchProfile> profiles` to `AppConfig`.
- [ ] Add TOML serialisation/deserialisation for `[[profiles]]` array in `app_config_io.cpp`.

### Phase 2: Startup selection

- [ ] In `App::initialize()` or `main()`, check for a `--profile <name>` CLI flag.
- [ ] If present, find the named profile in `config.profiles` and use it to configure the initial host.
- [ ] If absent, use the first profile (or fall back to the existing `host_type` / `nvim_args` fields for backwards compatibility).

### Phase 3: Command palette integration

- [ ] Expose profiles as selectable entries in the command palette (icebox `60 command-palette`) when that feature is implemented.
- [ ] For now: add a keybinding action `open_profile_picker` that shows an ImGui popup listing all profiles.

### Phase 4: Documentation

- [ ] Document in `docs/features.md`.
- [ ] Update `CLAUDE.md` config notes section.
- [ ] Add an example `config.toml` snippet to the docs.

## Acceptance criteria

- [ ] `draxul --profile zsh` launches with the `zsh` profile.
- [ ] A `config.toml` without `[[profiles]]` still works (backwards compatible).
- [ ] Profile with an unknown `host_type` logs a `WARN` and falls back to default.
- [ ] Profile picker ImGui popup shows all named profiles and applies the selected one.

## Interdependencies

- **Icebox `60 command-palette -feature`**: profiles integrate naturally as palette entries.
- **Icebox `37 hierarchical-config -feature`**: profiles are a form of per-launch config override; coordinate if that feature lands.
- **`14 config-layer-decoupling -refactor`**: adding profiles to `AppConfig` is simpler after the config layer is decoupled.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
