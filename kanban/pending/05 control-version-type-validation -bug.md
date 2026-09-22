# Reject malformed control protocol versions safely

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-control/src/control_codec.cpp:180` reads `version` with throwing `value<int>()` before authentication. Sending `{"version":"1"}` or a null version can escape `handle_frame` and terminate a transport listener thread’s process.

**Investigation**

- [x] Exercise malformed versions through the POSIX control transport on macOS.
- [ ] Exercise malformed versions through the Windows control transport.

**Fix strategy**

- [x] Validate the version’s type and range before conversion.
- [x] Contain frame-processing exceptions and return a structured failure.

**Acceptance criteria**

- [x] String, null, container, and oversized versions fail without disconnecting healthy clients or terminating the process.
- [x] Valid protocol versions continue working.
