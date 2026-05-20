# WI 116 — App::dispatch_to_nvim_host Uses Debug-String Heuristic

**Type:** Bug  
**Severity:** Medium (correctness bug — wrong pane targeted if name changes)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`app/app.cpp::dispatch_to_nvim_host()` (approx L1294) locates the target Neovim pane by checking:

```cpp
host.debug_state().name == "nvim"
```

This is a debug-string heuristic, not a capability boundary. Problems:
1. If a host's debug name is changed, the dispatch silently stops working.
2. If a non-nvim host happens to return `"nvim"` from `debug_state().name`, it gets incorrectly targeted.
3. There is no way to dispatch to a *specific* Neovim pane in a multi-pane workspace — the first matching name wins.

This was flagged by Gemini as an example of capability probing via debug strings rather than typed interfaces.

---

## Investigation Steps

- [x] Find `dispatch_to_nvim_host` in `app/app.cpp` — confirm the `debug_state().name == "nvim"` pattern
- [x] Identify all callers of `dispatch_to_nvim_host`
- [x] Check whether `IHost` or `GridHostBase` has an appropriate type-safe query (e.g. `is_nvim_host()`, a `NvimCapability` interface, or a typed downcast to `NvimHost`)

---

## Fix Strategy

**Option A (preferred):** Add a capability method to `IHost`:
```cpp
virtual bool is_nvim_host() const { return false; }
```
Override in `NvimHost` to return `true`. Replace the string comparison with `host.is_nvim_host()`.

**Option B:** Use `dynamic_cast<NvimHost*>(&host)` — acceptable given this is a one-time startup/action path, not a hot loop.

Either way, document *which* pane is targeted when multiple Neovim panes are open (first focused? first in tree?).

---

## Acceptance Criteria

- [x] `grep "debug_state().name" app/app.cpp` returns no hits relating to dispatch logic.
- [x] Dispatch correctly targets a Neovim host when multiple pane types coexist.
- [ ] **WI 122** (mixed-host dispatch test) passes with the fixed implementation.
- [ ] CI green.

## Status

Replaced the `host.debug_state().name == "nvim"` heuristic in
`App::dispatch_to_nvim_host` with a typed capability query
`IHost::is_nvim_host()` (default `false`), overridden in `NvimHost` to return
`true`. The selection policy is preserved (first matching host in
`HostManager::for_each_host` iteration order wins) and documented inline.

Files changed:
- `libs/draxul-host/include/draxul/host.h` — added `virtual bool is_nvim_host() const`
- `libs/draxul-host/src/nvim_host.h` — override returning `true`
- `app/app.cpp` — `dispatch_to_nvim_host` now uses `host.is_nvim_host()`

Build: `cmake --build build --target draxul draxul-tests` succeeded. The
acceptance test for mixed-host dispatch is tracked separately under WI 122 and
not added here.

---

## Interdependencies

- **WI 122** (mixed-host dispatch test) is the acceptance test for this fix — write the test after the fix lands.
