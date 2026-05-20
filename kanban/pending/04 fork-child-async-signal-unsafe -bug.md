# fork() child: non-async-signal-safe allocations before execvp

**Severity:** HIGH  
**Files:** `libs/draxul-host/src/unix_pty_process.cpp:117–127`, `libs/draxul-nvim/src/nvim_process.cpp:350–370`  
**Source:** review-bugs-consensus BUG-05 (gpt)

## Bug Description

Both `spawn()` paths perform `std::string` and `std::vector` heap allocations in the fork child — after `fork()` but before `execvp`. Draxul is already multithreaded at this point (RPC reader thread, PTY reader threads, session threads). If any other thread holds the internal `malloc` lock at the moment of `fork()`, the child inherits the locked mutex in a state it can never unlock. The child then deadlocks on the first heap allocation, producing a silent blank pane or failed nvim launch.

```cpp
// unix_pty_process.cpp — child path:
std::string login_argv0 = "-";       // heap alloc after fork
std::vector<const char*> argv;       // heap alloc after fork

// nvim_process.cpp — child path:
std::vector<std::string> argv_storage;   // heap alloc after fork
argv_storage.push_back(nvim_path);       // heap alloc after fork
```

POSIX requires the child of `fork()` in a multithreaded process to call only async-signal-safe functions between `fork()` and `execve()`. Heap allocation (`new`, `std::string`, `std::vector`) is not async-signal-safe.

**Trigger:** Pane open or nvim spawn while another thread is inside `malloc`. Low probability per event; cumulative over a busy session.

## Investigation

- [ ] Enumerate all C++ allocations between `fork()` and `execvp`/`_exit` in both files
- [ ] Check whether `setenv()` (called in `unix_pty_process.cpp:109`) is async-signal-safe — it is NOT
- [ ] Assess whether `posix_spawn()` is a viable drop-in for either call site
- [ ] Identify the minimum pre-fork data that must be prepared to make the child path allocation-free

## Fix Strategy

Option A — Pre-build argv in parent (recommended, POSIX-compliant):
- [ ] Build `std::vector<std::string>` and `std::vector<const char*>` in the parent **before** `fork()`
- [ ] Pass a raw `char* const*` argv pointer into the child path (the strings are valid for the duration since the parent waits or owns them)
- [ ] Replace `setenv()` in child with `putenv()` using a pre-built string, or use `execve()` with a pre-built envp array
- [ ] Child path becomes: `dup2`, `close` loop, `execve`/`execvp`, `_exit`

Option B — `posix_spawn()`:
- [ ] Replace both `fork()`/`exec()` patterns with `posix_spawn()` using `posix_spawn_file_actions_t` for the dup2 setup
- [ ] Avoids the entire async-signal-safe problem at the cost of less control over the child environment

## Acceptance Criteria

- [ ] No `std::string`, `std::vector`, `new`, `malloc`, or `setenv` calls between `fork()` and `execve()` in either file
- [ ] Shell pane and nvim spawn work correctly under `mac-tsan` preset with background threads active
- [ ] Smoke test (`py do.py smoke`) passes after the change
- [ ] No regression in pane open/close lifecycle tests
