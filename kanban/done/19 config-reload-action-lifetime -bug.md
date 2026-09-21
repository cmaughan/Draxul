# Own action text across configuration reload

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`app/input_dispatcher.cpp:389` retains an action view into keybindings. Executing `reload_config` replaces their storage at `app/app.cpp:686`, after which trace logging reads the dangling view at `input_dispatcher.cpp:399`.

**Investigation**

- [x] Dispatch a reload binding with input trace logging enabled and changed keybindings.

**Fix strategy**

- [x] Copy the selected action before execution, or otherwise retain its owning storage.
- [x] Inspect other action dispatch paths for equivalent reentrant invalidation.

**Acceptance criteria**

- [x] Trace-enabled reload has no invalid memory access.
- [x] Logs retain the dispatched action name, and new bindings work afterward.
