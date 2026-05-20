# WI 100 — nvimhost-partial-init-terminate

**Type:** bug  
**Priority:** 0 (process abort on any startup failure after reader thread starts)  
**Source:** review-bugs-consensus.md §C1 [GPT]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimHost::initialize_host()` (`libs/draxul-host/src/nvim_host.cpp:24–64`) calls
`rpc_.initialize()` at line 34, which starts the reader thread and stores it in
`NvimRpc::Impl::reader_thread_` (a plain `std::thread`).

If `attach_ui()` (line 54) or `execute_startup_commands()` (line 56) returns `false`,
`initialize_host()` returns `false`. `HostManager` then destroys the `unique_ptr<IHost>` it just
allocated, destroying `NvimHost`. This calls:

- `~NvimRpc()` = default
- `~NvimRpc::Impl()` = default (no destructor defined)
- `~std::thread()` on a **still-joinable** `reader_thread_`
- `std::terminate()` — process aborts.

The spawned `nvim` child process is also orphaned.

**Trigger:** Any startup failure after line 34 — `nvim_ui_attach` rejection, a bad startup command,
or Neovim crashing immediately on first attach.

---

## Investigation

- [ ] Read `libs/draxul-host/src/nvim_host.cpp:24–64` — confirm the reader thread is started before
  the failure branches.
- [ ] Read `libs/draxul-nvim/src/rpc.cpp:18–37` (`NvimRpc::Impl`) — confirm `Impl` has no
  destructor and `reader_thread_` is a plain `std::thread`.
- [ ] Read `app/host_manager.cpp` near line 455 — confirm `new_host` unique_ptr is destroyed on
  init failure without calling `shutdown()`.
- [ ] Check whether any base-class destructor in the `IHost` hierarchy calls `shutdown()`.

---

## Fix Strategy

**Option A (preferred — explicit cleanup):** Add a cleanup helper in `NvimHost` and call it on
every failure path after `rpc_.initialize()`:

```cpp
void NvimHost::abort_startup()
{
    rpc_.close();
    ui_request_worker_.stop();
    nvim_process_.shutdown();
    rpc_.shutdown();
}
```

Call `abort_startup()` before each `return false` in `initialize_host()` that comes after line 34.

**Option B (RAII — safer long-term):** Give `NvimRpc::Impl` a destructor that calls `close()` /
`join()` automatically:

```cpp
~Impl()
{
    running_.store(false);
    response_cv_.notify_all();
    if (reader_thread_.joinable())
        reader_thread_.join();
}
```

With Option B the default `~NvimRpc()` is safe without explicit cleanup.

- [ ] Apply chosen option.
- [ ] Ensure `nvim_process_.shutdown()` is called before `rpc_.shutdown()` so the pipe closes and
  the reader thread gets an EOF instead of blocking indefinitely.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] Starting Draxul with a deliberately broken `nvim` command (`--host bad_cmd`) exits cleanly
  with an error message rather than crashing.
- [ ] No `std::terminate` / crash on any `initialize_host()` failure path.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 101** (reader-thread exception guard) — fix this first; without a safe teardown the reader
  thread is also dangerous to stop.
- **WI 103** (startup callback race) — both touch the `rpc_.initialize()` call site; fix in the
  same pass.
