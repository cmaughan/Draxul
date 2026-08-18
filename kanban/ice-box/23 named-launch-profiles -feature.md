# Feature: Named Launch Profiles in config.toml

**Type:** feature
**Priority:** 23
**Source:** Gemini review

## Overview

Users want to launch Draxul with different host configurations without editing
`config.toml` each time. Profiles must use the generic provider/plugin identity and a
bounded launch descriptor rather than compile-time product host kinds.

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
name = "code-map"
provider = "plugin:dev.draxul.megacity"
config_json = '{"mode":"city"}'
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
      std::string provider; // built-in provider or plugin:<manifest-id>
      std::vector<std::string> args;
      std::string cwd;
      std::string config_json; // bounded provider launch descriptor
  };
  ```
- [ ] Add `std::vector<LaunchProfile> profiles` to `AppConfig`.
- [ ] Add TOML serialisation/deserialisation for `[[profiles]]` array in `app_config_io.cpp`.

### Phase 2: Startup selection

- [ ] In `App::initialize()` or `main()`, check for a `--profile <name>` CLI flag.
- [ ] If present, find the named profile in `config.profiles` and use it to configure the initial host.
- [ ] If absent, use the first profile (or fall back to the existing `host_type` / `nvim_args` fields for backwards compatibility).

### Phase 3: Command palette integration

- [ ] Expose profiles as selectable entries in the delivered command palette.
- [ ] For now: add a keybinding action `open_profile_picker` that shows an ImGui popup listing all profiles.

### Phase 4: Documentation

- [ ] Document in `docs/features.md`.
- [ ] Update `CLAUDE.md` config notes section.
- [ ] Add an example `config.toml` snippet to the docs.

## Acceptance criteria

- [ ] `draxul --profile zsh` launches with the `zsh` profile.
- [ ] A `config.toml` without `[[profiles]]` still works (backwards compatible).
- [ ] Profile with an unavailable provider reports the exact provider and does not
      silently launch a different host.
- [ ] Profile picker ImGui popup shows all named profiles and applies the selected one.

## Interdependencies

- `kanban/ice-box/37 hierarchical-config -feature.md` may later define profile overlay rules.
- The command palette, provider metadata, and config-layer boundaries are already available.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
