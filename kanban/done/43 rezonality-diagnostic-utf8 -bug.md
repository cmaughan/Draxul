# Make diagnostic publication tolerant of invalid UTF-8

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/diagnostics.cpp:33` truncates strings by byte count, while `:180` serializes with strict UTF-8 handling. A multibyte character crossing the message limit, or invalid compiler bytes, can throw through publication and terminate the application.

**Investigation**

- [x] Exercise invalid compiler text and multibyte messages at each truncation boundary.

**Fix strategy**

- [x] Sanitize invalid UTF-8 and truncate complete characters.
- [x] Make diagnostic serialization failure recoverable.

**Acceptance criteria**

- [x] Publication always produces valid bounded JSON or a controlled error.
- [x] Broken shader diagnostics cannot destroy the active scene or prevent later recovery.

**Completion evidence**

- Publication coverage crosses the byte limit with two-, three-, and four-byte
  characters, retains an exactly bounded complete character, sanitizes malformed
  compiler bytes, and parses every emitted document as JSON.
- A failed filesystem publication returns a controlled error; repairing the
  destination allows the same publisher to publish a valid later diagnostic.
- Focused diagnostics coverage passed 35 assertions across two cases. The core
  plus Rezonality aggregate passed all 29 CTest entries in 106.80 seconds,
  including live invalid-edit/recovery coverage; same-cache Metal smoke passed.
