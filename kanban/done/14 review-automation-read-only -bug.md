# Enforce read-only review automation

**Type:** bug
**Priority:** 14
**Raised by:** GPT/Codex

## Problem

`scripts/ask_agent.py` and the provider-specific review routines in `do.py` tell unattended agents to review only while enabling write/edit tools, permission bypass, full-auto, or `danger-full-access`. In a shared moving checkout, prompt compliance is the only write barrier.

## Resolution

Resolved by the repo-scoped `draxul-review` and `draxul-preflight` skills. The
unsafe `do.py` review commands and legacy agent scripts were retired instead of
retaining their compatibility surface.

## Implementation plan

- [x] Define one review-safe policy in the shared skill runner.
- [x] Invoke Codex in a read-only sandbox without approval bypass.
- [x] Give every provider plan/read-only flags and a separate disposable repository snapshot.
- [x] Have agents return only response text; the parent owns atomic artifact writes.
- [x] Reject unsafe output and known authentication/error responses.
- [x] Keep synthesis isolated and source-read-only.
- [x] Remove provider-specific review orchestration from `do.py`.
- [x] Document the trust boundary and temporary snapshot lifecycle in the skills.

## Tests

- [x] Test provider argv/tool allowlists and model passthrough.
- [x] Prove a malicious fixture can modify only its disposable snapshot.
- [x] Replace failed reviewers' stable artifacts with explicit failure stubs.

## Acceptance criteria

- [x] Review-only automation has an enforced filesystem/tool boundary.
- [x] Parent-owned output is atomic and validated before replacing stable review files.
- [x] The replacement skill supports explicit/default/all panels and separate synthesis.

## Dependencies and parallelism

Distinct from iceboxed agent-driver deduplication. A tooling sub-agent can implement this without touching product code.

<model>GPT-5 Codex</model>
