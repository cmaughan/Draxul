# Submit agent prompts automatically

**Priority:** P2
**Summary:** `agent prompt --text ...` should submit the request in one command; `agent send` remains text-only.

- [x] Append Enter for `agent prompt` in the same deduplicated input request, preserving `agent send` bytes.
- [x] Cover CLI request routing, Unicode/text fidelity, one submission, and input-size boundaries.
- [x] Update CLI help, feature documentation and the Draxul skill with explicit submission guidance.
- [x] Run the core aggregate, same-cache Release smoke, and skill validation.

Keep this a CLI semantic distinction. The existing `agent.send_text` endpoint already accepts terminal input bytes and deduplicates mutations. Include the Enter byte in its 64 KiB budget. No separate key request, protocol change, server restart, or mutation of the user's live agent is needed.

## Completed 2026-10-05

- The parsed `prompt` command marks text for submission. Request construction appends the same carriage-return byte used for Enter, sending text and Enter together through `agent.send_text` with one request ID. The `send` command leaves text unchanged.
- Parser validation rejects missing/empty text and enforces the 64 KiB byte limit, reserving one byte for Enter on prompts. No wire change or new server behavior is required.
- CLI integration coverage verifies exact Unicode/quoted/multiline payloads, the target instance ID, a single deduplicated request, and unchanged JSON results for both verbs. Boundary checks prove the largest accepted prompt produces exactly 64 KiB including Enter, while raw send retains its full 64 KiB allowance.
- Updated CLI help, `docs/features.md`, and `.agents/skills/draxul/SKILL.md`. The skill previously listed `prompt` without submission guidance; it now explains automatic Enter, text-only `send`, no extra Enter after `prompt`, and the local `dr rel`/`drr rel` workflow.
- `python3 do.py test release`: all 56 core CTest entries passed in 21.12 seconds. Incremental build was not timed separately. `python3 do.py smoke release --skip-build`: passed against the same cache. Skill Creator `quick_validate.py .agents/skills/draxul` passed. No renderer changes or additional platform gates.
- Rebuilt Release executable is ready for `dr rel`/`drr rel`. User agents were not prompted or interrupted during validation. No server restart is needed for this CLI-only submission change.
