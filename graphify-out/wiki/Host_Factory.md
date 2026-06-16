# Host Factory

> 12 nodes

## Key Concepts

- **register_builtin_host_providers()** (4 connections) — `libs/draxul-host/src/host_factory.cpp`
- **create_host()** (4 connections) — `libs/draxul-host/src/host_factory.cpp`
- **create_powershell_host()** (4 connections) — `libs/draxul-host/src/shell_host_win.cpp`
- **create_wsl_host()** (4 connections) — `libs/draxul-host/src/shell_host_win.cpp`
- **create_shell_host()** (3 connections) — `libs/draxul-host/src/shell_host_win.cpp`
- **unique_ptr** (3 connections) — `libs/draxul-host/src/shell_host_win.cpp`
- **IHost** (3 connections) — `libs/draxul-host/src/shell_host_win.cpp`
- **host_factory.cpp** (2 connections) — `libs/draxul-host/src/host_factory.cpp`
- **HostProviderRegistry** (1 connections) — `libs/draxul-host/src/host_factory.cpp`
- **unique_ptr** (1 connections) — `libs/draxul-host/src/host_factory.cpp`
- **IHost** (1 connections) — `libs/draxul-host/src/host_factory.cpp`
- **HostKind** (1 connections) — `libs/draxul-host/src/host_factory.cpp`

## Relationships

- [[Shell Host Win 2]] (3 shared connections)

## Source Files

- `libs/draxul-host/src/host_factory.cpp`
- `libs/draxul-host/src/shell_host_win.cpp`

## Audit Trail

- EXTRACTED: 27 (87%)
- INFERRED: 4 (13%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [[index]] to navigate.*