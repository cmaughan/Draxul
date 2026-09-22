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
- [ ] Terminal spawning remains free of unwanted Draxul-owned console flashes.
- [ ] Windows spawn and smoke tests pass.

## Windows validation (2026-09-22)

The Windows ConPTY suite now covers failed spawn followed by a successful retry
and statically rejects `EnumWindows`, `ConsoleWindowClass`, and `ShowWindow(` in
the production spawn path. Removing every desktop-wide enumeration/hide API is
stronger than timing a concurrent unrelated console: there is no remaining path
that can discover or hide it. `py do.py test debug --products` passed; the
same-cache smoke and the manual no-flash observation remain open above.
