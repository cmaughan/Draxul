# Prevent lost session-client stop and fallback wakes

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-client/src/remote_session_client.cpp:59` changes `stopping_`, and `:291` changes `externally_fed_`, without the mutex used by the untimed wait at `:811`. A race can hang shutdown or prevent legacy polling from starting.

**Investigation**

- [ ] Force each transition between predicate evaluation and entry into the external-feed wait.

**Fix strategy**

- [x] Update wait predicates under `mutex_`, then notify.
- [x] Audit startup and fallback transitions for consistent synchronization.

**Acceptance criteria**

- [x] An idle externally fed client always stops promptly.
- [x] Legacy fallback begins without needing an unrelated command.
- [x] Coordinate regression techniques with existing hidden-terminal work.
