# Busy-process close guard

**Type:** feature
**Priority:** 44
**Raised by:** GPT/Codex

## User need

Warn before closing a pane/tab/session that owns an active foreground job, while allowing clean shells and deliberate force-close.

## Implementation plan

- [ ] Define an optional `CloseRisk` capability/result with reason, process summary, confidence, and whether graceful close is supported.
- [ ] For POSIX PTYs compare the foreground process group with the shell; for Windows ConPTY use the best supported process-tree/job signal and document limitations.
- [ ] Treat unknown as unknown, not definitely safe; avoid expensive process enumeration every frame.
- [ ] Query only on destructive actions and aggregate risks across tab/session closes.
- [ ] Add a native confirmation overlay with Cancel, Close Anyway, and optional graceful interrupt/terminate where safe.
- [ ] Bypass prompts for clean exited panes and support an explicit force-close command path.
- [ ] Keep non-process hosts free to provide their own close risk later (for example unsaved documents).

## Tests and acceptance

- [ ] Fake-capability tests cover safe/busy/unknown, one/many panes, clean exit, and force close.
- [ ] Platform tests launch a shell with/without a foreground child and verify classification.
- [ ] Cancel leaves topology/focus/processes unchanged; confirm follows existing shutdown deadlines.
- [ ] Update docs and run lifecycle tests/smoke on both platforms.

## Dependencies and parallelism

Benefits from items 12 and 22. Cross-platform process detection can be delegated after the capability contract is fixed.

<model>GPT-5 Codex</model>
