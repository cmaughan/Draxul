# WI 28 — RPC timeout user feedback (toast when Neovim hangs)

**Type:** feature  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 7

---

## Goal

When an RPC request to Neovim exceeds the timeout threshold (`kRpcRequestTimeout = 5s`), show a dismissible toast notification to the user so they know why the GUI is unresponsive. Currently only a `WARN` log is emitted.

---

## Current behaviour

`NvimRpc::send_request()` times out after 5 seconds, logs `WARN`, and returns an error. The GUI becomes unresponsive with no visible indication. Users assume the app has crashed.

---

## Implementation Plan

- [ ] In `NvimRpc::send_request()` (or its error-return path), after a timeout, call `App::push_toast()` with a message like: `"Neovim is not responding (request timed out after 5s)"`.
- [ ] Make the toast non-auto-dismissing (or use a long duration like 30s) so the user sees it.
- [ ] Add a "Dismiss" action or auto-dismiss once Neovim becomes responsive again (i.e. a subsequent successful response clears the toast).
- [ ] Make the timeout configurable in `config.toml` (e.g. `rpc_timeout_s = 5.0`), validated in `AppConfig`.
- [ ] Add exponential backoff for repeated timeouts rather than always waiting 5s for the next request: 5s → 10s → 20s (cap at 60s).
- [ ] Document the new config key in `docs/features.md`.

---

## Notes for the agent

- The `push_toast()` call from inside `NvimRpc` requires a callback/interface — `NvimRpc` should not directly reference `App`. Use an existing callback pattern or inject a `std::function<void(std::string)> on_timeout_toast` into `NvimRpc::Deps`.
- Consider surfacing the unresponsive state in the diagnostics overlay as well (F12).

---

## Interdependencies

- **Requires Phase 1 bugs (WI 04-07) to be solid** — the RPC pipeline should be hardened before adding user-visible timeout behaviour on top.
- WI 24 (unified Result type) — the timeout error return should use the standard `Result` type.
- WI 12 (toast idle wake gap) must be fixed, otherwise the timeout toast might not appear on an idle app.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
