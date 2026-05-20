# 30 Remote Neovim Attach

## Why This Exists

Draxul currently only supports spawning a local `nvim --embed` child process. Users who work on
remote machines via SSH frequently want to connect Draxul to an existing `nvim --listen` socket
(TCP or Unix domain) rather than spawning a new process.

The `IRpcChannel` interface is the right abstraction point — it is already used cleanly in both
`NvimInput` and tests. Adding a socket-based `IRpcChannel` implementation would enable remote
attach without changing any UI or renderer code.

**Source:** `libs/draxul-nvim/include/draxul/nvim.h` (`IRpcChannel`), `libs/draxul-nvim/src/nvim_process.cpp`.
**Raised by:** Claude, GPT (both list remote attach as a top QoL feature).

## Goal

Add a `--attach <host:port>` (TCP) or `--attach <path>` (Unix socket) CLI flag that causes
Draxul to connect to an already-running Neovim instance rather than spawning one.

## Implementation Plan

- [ ] Read `libs/draxul-nvim/include/draxul/nvim.h` and `libs/draxul-nvim/src/` to understand `IRpcChannel` and how `NvimRpc` uses it.
- [ ] Read `app/main.cpp` and `app/app.h` to understand how `NvimProcess` is currently started.
- [ ] Create `libs/draxul-nvim/src/socket_rpc_channel.h/cpp`:
  - Implements `IRpcChannel` using a TCP socket or Unix domain socket.
  - On connect: performs a Neovim API handshake (`nvim_get_api_info`).
  - On disconnect: signals the app to shut down gracefully.
- [ ] Add `--attach <address>` to `AppOptions` / `main.cpp` argument parsing.
- [ ] In `App::initialize()`, branch on whether `--attach` was given:
  - If yes: create `SocketRpcChannel` and skip `NvimProcess::spawn()`.
  - If no: existing `NvimProcess::spawn()` path unchanged.
- [ ] Test manually: start `nvim --listen 127.0.0.1:7777`, launch Draxul with `--attach 127.0.0.1:7777`.
- [ ] Run `ctest --test-dir build`.

## Notes

Platform socket APIs differ between Windows (`WinSock2`) and macOS (`POSIX sockets`).
Use SDL3's `SDL_net` or `std::net` (C++26) if available, otherwise wrap the platform APIs
behind a `connect_socket(address)` function with `#ifdef`s.

## Sub-Agent Split

- One agent on `SocketRpcChannel` implementation (platform I/O layer).
- One agent on CLI parsing, `App` branching, and connection lifecycle.
