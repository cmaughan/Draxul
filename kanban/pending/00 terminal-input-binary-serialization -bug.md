# Preserve arbitrary terminal input across Session transport
**Severity:** CRITICAL  
**Source:** Codex #1; `libs/draxul-client/src/remote_session_coordinator.cpp:1659`.

Legacy mouse coordinates can contain `0x80`, and paste chunking can split UTF-8 characters. Strict JSON serialization throws outside the worker boundary and terminates the GUI.

**Investigation**

- [x] Trace arbitrary input bytes and chunk boundaries through current client, protocol, and server paths.

**Fix strategy**

- [x] Implement lossless binary-safe wire encoding and matching server decoding; retain current-format version enforcement.
- [x] Contain worker exceptions and expose a recoverable failure without silently discarding accepted input.

**Acceptance criteria**

- [x] Windows transport tests preserve legacy mouse column 95, arbitrary bytes, and split multibyte pastes without a worker exception escaping.
- [ ] Validate the same transport cases on macOS CI.
- [x] Run core aggregate tests and a same-cache smoke on Windows.

## Implementation notes

- Current stream and short-control clients encode each terminal input chunk in
  `input_base64`; the server decodes it with strict canonical padding and the
  existing 64 KiB bound. Older JSON `text` clients remain accepted, but a
  request cannot contain both fields. Session/control protocol version checks
  remain in place.
- Stream serialization failures now return a transport error so in-flight
  terminal commands can requeue through the existing fallback. Unexpected
  worker exceptions publish a terminal error and stop the affected worker
  without terminating the GUI.
- Windows Debug core aggregate passed 25/25 CTest entries. The same-cache
  single-pane `--smoke-test --host nvim` passed in 8.6 seconds, and the rebuilt
  Release app passed `--smoke-test`. The default `py do.py smoke --skip-build`
  timed out at 30 seconds while loading the existing nine-pane shared Session;
  an identical Debug smoke with a 90-second bound completed successfully.
- [Build workflow run 35989867854](https://github.com/cmaughan/Draxul/actions/runs/35989867854)
  stopped in both hosted jobs during checkout because the workflow token cannot
  read the private `draxul-pcbview` submodule. macOS transport execution remains open.
