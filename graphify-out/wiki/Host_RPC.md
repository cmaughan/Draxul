# Host RPC

> 16 nodes

## Key Concepts

- **thread** (14 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **initialize()** (4 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **conpty_process.h** (2 connections) — `libs/draxul-host/src/conpty_process.h`
- **draxul()** (2 connections) — `libs/draxul-host/src/conpty_process.h`
- **unix_pty_process.h** (2 connections) — `libs/draxul-host/src/unix_pty_process.h`
- **draxul()** (2 connections) — `libs/draxul-host/src/unix_pty_process.h`
- **NvimProcess** (2 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **session_attach.h** (2 connections) — `libs/draxul-runtime-support/include/draxul/session_attach.h`
- **draxul()** (2 connections) — `libs/draxul-runtime-support/include/draxul/session_attach.h`
- **thread_check.h** (2 connections) — `libs/draxul-types/include/draxul/thread_check.h`
- **draxul()** (2 connections) — `libs/draxul-types/include/draxul/thread_check.h`
- **namespace** (1 connections) — `libs/draxul-host/src/conpty_process.h`
- **namespace** (1 connections) — `libs/draxul-host/src/unix_pty_process.h`
- **RpcCallbacks** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **namespace** (1 connections) — `libs/draxul-runtime-support/include/draxul/session_attach.h`
- **namespace** (1 connections) — `libs/draxul-types/include/draxul/thread_check.h`

## Relationships

- [[Nvim RPC 2]] (3 shared connections)
- [[Nvim RPC]] (2 shared connections)
- [[Conpty Process]] (1 shared connections)
- [[Nvim Host]] (1 shared connections)
- [[Community 264]] (1 shared connections)
- [[Vulkan Renderer 4]] (1 shared connections)
- [[Community 265]] (1 shared connections)
- [[Grid Grid 2]] (1 shared connections)

## Source Files

- `libs/draxul-host/src/conpty_process.h`
- `libs/draxul-host/src/unix_pty_process.h`
- `libs/draxul-nvim/src/rpc.cpp`
- `libs/draxul-runtime-support/include/draxul/session_attach.h`
- `libs/draxul-types/include/draxul/thread_check.h`

## Audit Trail

- EXTRACTED: 41 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*