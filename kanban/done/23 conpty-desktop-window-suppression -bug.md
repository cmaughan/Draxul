# Restrict ConPTY console-window suppression

**Severity:** HIGH  
**Type:** Bug

## Bug description

ConPTY spawn polls every top-level desktop window for eight seconds and hides any new `ConsoleWindowClass`, including windows owned by unrelated applications.

**Trigger:** Open another application’s console within eight seconds of a Draxul terminal spawn or failed spawn attempt.

## Investigation

- [x] Determine why visible consoles still require suppression with current ConPTY creation flags.
- [x] Trace the spawned child and conhost process ownership relationships.
- [x] Add coverage for failed spawn and concurrent unrelated console creation.

## Fix strategy

- [x] Prefer removing the global suppression workaround.
- [x] If suppression remains necessary, start only after successful process creation.
- [x] Filter candidates to the spawned PID or verified descendants and stop once handled.
- [x] Avoid detached polling that outlives its owning spawn operation unnecessarily.

## Acceptance criteria

- [x] Draxul never hides a console window owned by an unrelated process.
- [x] Failed spawns start no suppression worker.
- [x] Terminal spawning remains free of unwanted Draxul-owned console flashes.
- [x] Windows spawn and smoke tests pass.

## Windows validation (2026-09-22)

The Windows ConPTY suite now covers failed spawn followed by a successful retry
and statically rejects `EnumWindows`, `ConsoleWindowClass`, and `ShowWindow(` in
the production spawn path. Removing every desktop-wide enumeration/hide API is
stronger than timing a concurrent unrelated console: there is no remaining path
that can discover or hide it. The remaining spawn and smoke evidence is recorded
below.

## No-flash closeout (2026-09-22)

- A Win32 `SetWinEventHook(EVENT_OBJECT_SHOW)` probe watched the interactive
  desktop while the real `[conpty_process][windows][spawn]` cases ran 50 times.
  All 50 iterations passed and the probe observed zero visible
  `ConsoleWindowClass` show events, including short-lived windows that ordinary
  polling could miss.
- The final Windows Release products aggregate passed all 48 registered CTest
  entries, including the ConPTY spawn cases, and the same-cache Vulkan smoke
  passed with `--skip-build`.
