# Preserve arbitrary terminal input across Session transport
**Severity:** CRITICAL  
**Source:** Codex #1; `libs/draxul-client/src/remote_session_coordinator.cpp:1659`.

Legacy mouse coordinates can contain `0x80`, and paste chunking can split UTF-8 characters. Strict JSON serialization throws outside the worker boundary and terminates the GUI.

**Investigation**

- [ ] Trace arbitrary input bytes and chunk boundaries through current client, protocol, and server paths.

**Fix strategy**

- [ ] Implement lossless binary-safe wire encoding and matching server decoding; retain current-format version enforcement.
- [ ] Contain worker exceptions and expose a recoverable failure without silently discarding accepted input.

**Acceptance criteria**

- [ ] Legacy mouse column 95, arbitrary bytes, and split multibyte pastes arrive intact without terminating the GUI.
- [ ] Run core aggregate tests and a same-cache smoke; record Windows and macOS transport validation.
