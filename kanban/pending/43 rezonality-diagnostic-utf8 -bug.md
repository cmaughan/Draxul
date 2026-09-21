# Make diagnostic publication tolerant of invalid UTF-8

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/diagnostics.cpp:33` truncates strings by byte count, while `:180` serializes with strict UTF-8 handling. A multibyte character crossing the message limit, or invalid compiler bytes, can throw through publication and terminate the application.

**Investigation**

- [ ] Exercise invalid compiler text and multibyte messages at each truncation boundary.

**Fix strategy**

- [x] Sanitize invalid UTF-8 and truncate complete characters.
- [x] Make diagnostic serialization failure recoverable.

**Acceptance criteria**

- [x] Publication always produces valid bounded JSON or a controlled error.
- [x] Broken shader diagnostics cannot destroy the active scene or prevent later recovery.
