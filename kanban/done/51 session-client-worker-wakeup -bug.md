# Prevent lost session-client stop and fallback wakes

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-client/src/remote_session_client.cpp:59` changes `stopping_`, and `:291` changes `externally_fed_`, without the mutex used by the untimed wait at `:811`. A race can hang shutdown or prevent legacy polling from starting.

**Investigation**

- [x] Force each transition between predicate evaluation and entry into the external-feed wait.

The regression seam pauses one named client after its final false predicate
evaluation while the worker still owns the wait mutex. Separate threads then
request stop or legacy fallback and contend for that mutex until the wait
atomically releases it. This deterministically covers the former lost-notify
window without timing sleeps or an unrelated command wake.

Validated on macOS with the exact two race cases (2/2 cases, 7 assertions), the
core aggregate (23/23 CTest entries), and the same-cache headless smoke. The
aggregate was rerun outside the filesystem sandbox after the sandbox denied all
temporary Unix socket binds with `EPERM`.

**Fix strategy**

- [x] Update wait predicates under `mutex_`, then notify.
- [x] Audit startup and fallback transitions for consistent synchronization.

**Acceptance criteria**

- [x] An idle externally fed client always stops promptly.
- [x] Legacy fallback begins without needing an unrelated command.
- [x] Coordinate regression techniques with existing hidden-terminal work.
