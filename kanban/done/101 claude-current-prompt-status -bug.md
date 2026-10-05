# Recognize Claude's current input prompt and completion summary

**Priority:** P2
**Summary:** Claude appears as `unknown` even while its current bordered input prompt clearly shows it is waiting for input.

- [x] Recognize the current Claude composer, including non-breaking spaces and typed/multiline input.
- [x] Give current working/approval indicators precedence while excluding typed prompts and stale transcript text.
- [x] Recognize structured elapsed-time completion summaries and retain unknown for ambiguous output.
- [x] Validate status transitions through server projection, explain/wait APIs, core tests and Release smoke.

Observed live on 2026-10-05: `crisp-crow` was confidently discovered as Claude, but `agent explain` returned `ambiguous_terminal_evidence`. Its terminal had an elapsed-time completion summary followed by a bordered `❯` input row and the auto-mode footer. Fix shared evidence evaluation rather than special-casing the CLI; keep explanation output free of captured text. Do not interrupt the user's active Claude session for validation.

## Completed 2026-10-05

- Claude manifest version 2 recognizes a bordered current composer and known footer UI. Both ASCII and non-breaking spaces after the prompt are accepted. Wrapped draft contents are excluded from status matching, and numbered approval-menu choices are not treated as input prompts.
- Current progress/approval evidence in the footer or immediately above the composer overrides idle, with blocking evidence winning over progress. Historical framed prompts followed by ordinary output are rejected. Older transcript text above the current status area cannot override the current input box.
- Structured star-prefixed elapsed-time completion summaries yield done only when not superseded by a current input box or newer recognized evidence. Unrecognized text remains unknown; explanation fields contain only rule IDs/categories, not captured text.
- Added 17 screen fixtures exercised as successive observations through the real server agent service. Assertions cover list status, running state, explain rule/version/privacy and wait completion. Fixtures include the observed NBSP prompt/completion structure, multiline private drafts, active work, approvals, stale prompts, completion transitions and ambiguous text.
- `python3 do.py test release`: all 56 core CTest entries passed in 21.59 seconds. Incremental compilation was not separately timed. `python3 do.py smoke release --skip-build`: passed against the same Release cache. No render changes or separate platform gates.
- Release app/helper rebuilt. The running user server and active Claude terminal were not restarted or interrupted; restart the server when convenient to load the new evaluator.
