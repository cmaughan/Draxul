# Restrict ConPTY console-window suppression

**Severity:** HIGH  
**Type:** Bug

## Bug description

ConPTY spawn polls every top-level desktop window for eight seconds and hides any new `ConsoleWindowClass`, including windows owned by unrelated applications.

**Trigger:** Open another application’s console within eight seconds of a Draxul terminal spawn or failed spawn attempt.

## Investigation

- [ ] Determine why visible consoles still require suppression with current ConPTY creation flags.
- [ ] Trace the spawned child and conhost process ownership relationships.
- [ ] Add coverage for failed spawn and concurrent unrelated console creation.

## Fix strategy

- [ ] Prefer removing the global suppression workaround.
- [ ] If suppression remains necessary, start only after successful process creation.
- [ ] Filter candidates to the spawned PID or verified descendants and stop once handled.
- [ ] Avoid detached polling that outlives its owning spawn operation unnecessarily.

## Acceptance criteria

- [ ] Draxul never hides a console window owned by an unrelated process.
- [ ] Failed spawns start no suppression worker.
- [ ] Terminal spawning remains free of unwanted Draxul-owned console flashes.
- [ ] Windows spawn and smoke tests pass.
