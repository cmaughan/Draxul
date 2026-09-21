# Contain malformed layout requests on the session stream

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-server/src/topology_service.cpp:208` converts `dry_run` before checking its type; `:333` converts `direction` without validation. An authenticated stream request with `"dry_run":"yes"` can throw through `session_stream_service.cpp:305` and terminate the server.

**Investigation**

- [x] Exercise wrong-typed layout fields over both control and persistent-stream paths.

**Fix strategy**

- [x] Validate fields before conversion.
- [x] Catch dispatch exceptions per command, preserving correlation and replay behavior.

**Acceptance criteria**

- [x] Malformed layouts return structured errors without changing topology.
- [x] Other sessions remain usable after each rejected request.
- [x] Coordinate exception handling with the server filesystem card.
