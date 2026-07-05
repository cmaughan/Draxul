# Enforce read-only review automation

**Type:** bug
**Priority:** 14
**Raised by:** GPT/Codex

## Problem

`scripts/ask_agent.py` and `scripts/Run-Review.ps1` tell unattended agents to review only while enabling write/edit tools, permission bypass, full-auto, or `danger-full-access`. In a shared moving checkout, prompt compliance is the only write barrier.

## Implementation plan

- [ ] Define a single `review-safe` execution policy used by `do_review.py`, `ask_agent.py`, and `Run-Review.ps1`.
- [ ] Invoke Codex review agents with a read-only sandbox and no approval bypass.
- [ ] Give Claude/Gemini only genuinely read-only capabilities; if a runner cannot enforce that, run it against an isolated read-only snapshot.
- [ ] Have agents return review text to stdout/a designated result channel; only the parent process may write the named review file.
- [ ] Reject conflicting flags such as review-safe plus write/full-auto permissions.
- [ ] Keep consensus generation write-capable only for its explicit output/work-item phase, not for source edits.
- [ ] Move provider-specific review orchestration out of the general build/deploy body of `do.py`; keep `do.py` as a thin command dispatcher to the reviewed scripts.
- [ ] Document the trust boundary and cleanup of temporary snapshots/results.

## Tests

- [ ] Extend Python/PowerShell dry-run tests to assert exact argv/tool allowlists for every provider.
- [ ] Add a fixture agent that attempts a write and prove the review run cannot modify the source checkout.
- [ ] Verify a failed agent does not leave a partial review file presented as success.

## Acceptance criteria

- [ ] Review-only automation has an enforced filesystem/tool boundary.
- [ ] Parent-owned output is atomic and validated before replacing `review-latest.*.md`.
- [ ] Existing `do review*` commands retain their supported agent selection behavior.

## Dependencies and parallelism

Distinct from iceboxed agent-driver deduplication. A tooling sub-agent can implement this without touching product code.

<model>GPT-5 Codex</model>
