# WI 38 — pane-activity-badges

**Type:** feature  
**Priority:** Medium  
**Source:** review-consensus.md §6c — GPT  
**Produced by:** claude-sonnet-4-6

---

## Feature Description

Show a small visual badge on the tab and/or pane pill for panes that have background activity, specifically:

1. **Bell**: a shell `\a` (BEL) signal was received while the pane was not focused.
2. **Non-zero exit**: the last foreground command exited with a non-zero code (OSC 133 parsing is delivered; see `kanban/ice-box/24 osc133-shell-integration -feature.md`).
3. **Long-running**: a command has been running for longer than a configurable threshold (e.g., 30 seconds).

The badge should clear when the user focuses the pane. The intent is to allow users with many split panes to see at a glance which panes need attention without switching to each one.

---

## Investigation

- [ ] Read `app/chrome_host.cpp` — find where the tab bar and pane pills are rendered; identify where a badge indicator could be added.
- [ ] Read `libs/draxul-host/include/draxul/host.h` — check if `IHost` has a notification/activity API or if one needs to be added.
- [ ] Read `draxul-terminal-core` and the server terminal projection to find
      where BEL and shell marks become semantic state.
- [x] Reuse the delivered OSC 133 mark/exit-code model.
- [ ] Review `AppConfig` for any existing notification-related fields.

---

## Implementation Plan

### Step 1: Activity state in IHost

- [ ] Add an `ActivityFlags` enum or bitmask to `IHost`:
  ```cpp
  enum class ActivityFlag : uint8_t {
      none     = 0,
      bell     = 1 << 0,
      exit_err = 1 << 1,
      running  = 1 << 2,
  };
  ```
- [ ] Add `activity_flags()` getter and `clear_activity()` method (called by `PaneManager` when a host gains focus).

### Step 2: BEL detection

- [ ] In `terminal_host_base.cpp` (or the VT parser dispatch), when BEL is received and the pane is not focused, set `ActivityFlag::bell` on the host.
- [ ] Optionally emit the system bell sound (platform-specific, can be guarded behind a config flag).

### Step 3: Running process detection

- [ ] Extend the server terminal/agent projection with a bounded activity state;
      do not make the client inspect remote process trees.
- [ ] Derive running/exit state from server-owned process observation and shell
      marks. Keep Nvim/client-only products on an optional local capability.
- [ ] For `NvimHost`: always `false` (Neovim is always running; this badge is terminal-oriented).
- [ ] Track start-time of last "shell" state change; if `is_command_running()` is true for > threshold, set `ActivityFlag::running`.

### Step 4: Badge rendering in ChromeHost

- [ ] In the tab-bar render loop, after drawing the tab label, if the host's `activity_flags()` is non-zero, draw a small dot/circle (1–2 cells wide) with a color that varies by badge type (e.g., orange for bell, red for error, blue for running).
- [ ] Mirror the same indicator on pane pills.

### Step 5: Configuration

- [ ] Add to `AppConfig`:
  ```toml
  [notifications]
  running_badge_threshold_s = 30.0   # seconds before "running" badge appears
  show_bell_badge = true
  show_exit_badge = true             # uses delivered OSC 133 marks
  show_running_badge = true
  ```
- [ ] Document in `docs/features.md`.

### Step 6: Tests

- [ ] Unit test: deliver BEL to unfocused host → assert `ActivityFlag::bell` is set.
- [ ] Unit test: focus the host → assert flags are cleared.
- [ ] Render snapshot test: tab with bell badge has expected badge pixel.

---

## Acceptance Criteria

- [ ] Bell badge appears on unfocused pane tab when BEL is received.
- [ ] Badge clears when pane is focused.
- [ ] Running badge appears after the configured threshold.
- [ ] All new config fields are documented in `docs/features.md`.
- [ ] Smoke test passes.

---

## Dependencies

- [x] OSC 133 parsing/transport prerequisite is delivered.
- [ ] Coordinate with `kanban/ice-box/125 overlay-registry-refactor -refactor.md` and
      `kanban/ice-box/39 rightclick-context-menus -feature.md` because all touch Chrome.
