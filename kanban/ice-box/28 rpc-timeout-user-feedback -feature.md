# WI 28 — RPC timeout user feedback (toast when Neovim hangs)

**Type:** feature  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 7

---

## Goal

Surface startup or worker-thread Neovim RPC timeouts through a stable status that the
app can show as a toast and in diagnostics.

---

## Current behaviour

`NvimRpc::request()` times out and logs a warning. After initialization, main-thread
requests are rejected and UI requests run through `UiRequestWorker`, so a timeout no
longer blocks the render loop. The remaining gap is visible status/reporting for startup
and worker failures.

---

## Implementation Plan

- [ ] Return a typed timeout status from `NvimRpc::request()` through startup and
      `UiRequestWorker` result paths.
- [ ] Translate that status at the App/host boundary into a toast and diagnostics state.
- [ ] Make the toast non-auto-dismissing (or use a long duration like 30s) so the user sees it.
- [ ] Add a "Dismiss" action or auto-dismiss once Neovim becomes responsive again (i.e. a subsequent successful response clears the toast).
- [ ] Make the timeout configurable in `config.toml` (e.g. `rpc_timeout_s = 5.0`), validated in `AppConfig`.
- [ ] Coalesce repeated notifications without lengthening each request timeout.
- [ ] Document the new config key in `docs/features.md`.

---

## Notes for the agent

- The `push_toast()` call from inside `NvimRpc` requires a callback/interface — `NvimRpc` should not directly reference `App`. Use an existing callback pattern or inject a `std::function<void(std::string)> on_timeout_toast` into `NvimRpc::Deps`.
- Consider surfacing the unresponsive state in the diagnostics overlay as well (F12).

---

## Interdependencies

Typed results, toast delivery, and idle wake behavior are already available. Keep
transport code independent of `App` and surface UI policy through existing callbacks.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
