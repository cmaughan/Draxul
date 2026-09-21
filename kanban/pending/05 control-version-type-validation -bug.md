# Reject malformed control protocol versions safely

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-control/src/control_codec.cpp:180` reads `version` with throwing `value<int>()` before authentication. Sending `{"version":"1"}` or a null version can escape `handle_frame` and terminate a transport listener thread’s process.

**Investigation**

- [ ] Exercise malformed versions through POSIX and Windows control transports.

**Fix strategy**

- [ ] Validate the version’s type and range before conversion.
- [ ] Contain frame-processing exceptions and return a structured failure.

**Acceptance criteria**

- [ ] String, null, container, and oversized versions fail without disconnecting healthy clients or terminating the process.
- [ ] Valid protocol versions continue working.
