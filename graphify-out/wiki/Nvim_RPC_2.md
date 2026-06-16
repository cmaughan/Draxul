# Nvim RPC 2

> 23 nodes

## Key Concepts

- **NvimRpc::Impl** (26 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **condition_variable** (3 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **drain_notifications()** (3 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **ui_request_worker.h** (3 connections) — `libs/draxul-runtime-support/include/draxul/ui_request_worker.h`
- **deque** (2 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **RpcNotification** (2 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **draxul()** (2 connections) — `libs/draxul-runtime-support/include/draxul/ui_request_worker.h`
- **process_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **reader_thread_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **running_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **write_mutex_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **notif_mutex_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **notifications_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **response_mutex_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **response_cv_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **RpcResponse** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **responses_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **timed_out_msgids_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **next_msgid_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **read_failed_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **malformed_packet_count_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **read_buf_** (1 connections) — `libs/draxul-nvim/src/rpc.cpp`
- **namespace** (1 connections) — `libs/draxul-runtime-support/include/draxul/ui_request_worker.h`

## Relationships

- [[Nvim RPC]] (10 shared connections)
- [[Host RPC]] (3 shared connections)

## Source Files

- `libs/draxul-nvim/src/rpc.cpp`
- `libs/draxul-runtime-support/include/draxul/ui_request_worker.h`

## Audit Trail

- EXTRACTED: 57 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*