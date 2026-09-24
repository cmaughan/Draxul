---
name: draxul-review
description: Run independent Draxul repository reviews through multiple AI companies, write archived and latest Markdown reports, or synthesize selected reviews with a caller-supplied prompt. Use for multi-model code review, feature/bug/refactor review panels, consensus, comparison, or review summarization.
---

# Draxul multi-AI review

Use `scripts/review.py` for every provider call. Do not call reviewer CLIs directly.

## Generate reviews

1. Resolve the requested prompt file relative to the repository root. If the user supplied inline text, preserve it exactly in a temporary UTF-8 prompt file.
2. Choose reviewers:
   - No selection: use the two-model panel: OpenAI `gpt-6-astra` and Anthropic `claude-fable-5-1` (Claude Fable 5.1).
   - “All”: pass `--all` to include every healthy configured company.
   - Named reviewers: pass repeatable `--reviewer transport:model` values. Supported transports are `codex`, `claude`, `google`, `agy`, `gemini`, and `grok`.
3. Run from the repository root:

```text
py .agents/skills/draxul-review/scripts/review.py review --prompt-file <prompt> [--reviewer <transport:model> ... | --all] [--name <artifact-name>]
```

4. Arrange a status check every 10 minutes as described below. The runner saves each reviewer report and diagnostics immediately, updates the running manifest, then automatically produces consensus from that exact run. Report the immutable run directory, successful/failed reviewers, fallbacks, latest files, archived Repomix input, consensus path, and any created Kanban cards. Use `--no-consensus` only when the user requests independent reviews without synthesis.

Codex and Claude calls explicitly use **high reasoning effort**, including synthesis. Astra review/synthesis calls enable subagents with a maximum of four concurrent workers and explicitly set worker defaults to the selected model at high effort. Preflight disables delegation. Other optional transports retain their provider effort defaults.

The feature, bug, and refactor prompts all require an architecture map, explicit read-only subsystem delegation (up to four workers, no nested delegation), substantive investigation of every major area, coordinator verification of accepted findings, and a final cross-component pass. Workers share the same packed source and review-only restrictions. If delegation is unavailable, review sequentially and disclose the limitation. Reports include an investigation coverage table and unreviewed areas, with no finding quota or arbitrary report-length cap. Unresolved leads remain separate from accepted findings and Kanban proposals. Consensus uses the same evidence standard against current source and the exact selected reports.

Reviews use **one Repomix file and one coordinating review run per provider**, with no source segments, batches, or coverage receipts. Install the `repomix` CLI on PATH before running a review. The runner first freezes one source snapshot shared by the panel, recursively including initialized Git submodules (including dirty and non-ignored untracked files), and fails if any submodule is missing. Git metadata, ignored files, `plans/reviews/` archives, and previous `repomix-output.*` files are excluded.

The runner invokes Repomix once on that snapshot to generate `repomix-output.xml`, with the directory tree, original paths, line numbers, comments, and full source text (no compression or splitting). Repomix's binary and security filtering remain enabled; its diagnostics are saved in `logs/repomix.log`. The exact generated file is archived with size and SHA-256 provenance in `input.json` and the review manifest. Each reviewer gets a private copy of that same file plus `REVIEW_PROMPT.md`, and uses the packed file as its sole source input. Reviewers can read and search across embedded file sections and delegate bounded investigations within that run. They must state context or scope limitations honestly; one run does not guarantee exhaustive review of a large repository.

Generate and inspect the packed input without invoking providers:

```text
py .agents/skills/draxul-review/scripts/review.py review --prompt-file <prompt> --plan-only
```

The runner preflights providers after packing, runs their one-shot reviews concurrently, and owns all report writes. The default timeout is 90 minutes **per reviewer and synthesis**, with a separate preparation timeout of the same duration for Repomix. Each report is published as its reviewer finishes; a slow or failed peer cannot keep completed reports only in memory. After the panel finishes, successful reports are synthesized automatically by `codex:gpt-6-astra` at high effort. Partial panels produce explicitly partial consensus; no successful reports means no synthesis. Consensus failure preserves the reviews and returns a nonzero exit status. Persist real Codex, Claude, and Grok review sessions in their normal provider stores so TokenFu can retain tool-call and token telemetry; keep nonce-only preflight sessions ephemeral. Start fresh sessions and keep cross-session memory disabled.

## Monitor through completion

For every launched review, arrange a recurring 10-minute status check in the calling task using the host's supported scheduler (a Codex thread heartbeat when available). Reuse an existing monitor for the same run rather than creating duplicates. Record the exact run directory and monitor identifier so follow-ups inspect the correct run. If scheduling is unavailable, disclose that and keep checking in the active session; do not claim a future check is scheduled.

At each check, read the run's `manifest.json` and any linked consensus manifest, and verify matching process IDs before declaring a still-running or unexpectedly stopped process. Report each reviewer's state and elapsed time, saved report paths when available, the consensus state, and any error. Report every scheduled check even when work remains in progress; do not invent a completion percentage or restart providers automatically.

Continue until both the review panel and automatic consensus have reached terminal states. Review completion alone is not the stopping condition. Verify the summary and all listed Kanban files exist. If the user requested merging tasks, reconcile accepted findings against current root/product tracker lanes, merge supported refinements into the matching existing cards while preserving checked tasks and unrelated content, and remove only newly generated duplicate cards after preserving their useful content and repairing references. Never repeat a review merely to merge tasks.

Report final successes/failures, consensus and report paths, and created/merged work items, then disable or delete the monitor. If the runner stops unexpectedly or fails without starting consensus, report the failure and stop that monitor rather than polling a stale running manifest indefinitely.

## Default consensus and Kanban options

- `review` produces a consensus report and validated Kanban cards by default.
- Use `--no-kanban` for a consensus report without creating cards; `--kanban` explicitly enables the default behavior.
- Use `--no-consensus` for independent reports only. It skips both synthesis and card creation and cannot be combined with `--consensus-prompt`.
- The default consensus prompt is `plans/prompts/consensus_<review-prompt-stem>.md` (for example `consensus_review_bugs.md`); a general reconciliation prompt is used when no matching saved prompt exists.
- `--consensus-prompt <path>` overrides the saved prompt. `--summarizer transport:model` overrides Astra only when the user names a synthesizer.
- Consensus always uses the current immutable review run, not mutable latest-file globs. Its state and summary-run link are recorded in the review manifest, separately from reviewer completion status.
- `--plan-only` generates the Repomix input without invoking reviewers or consensus.

## Summarize reviews

Resolve exactly which reviews the user selected. Prefer an immutable run id when available.

```text
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --run <run-id>
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --input <review.md> [--input <review.md> ...]
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --glob <pattern>
```

Standalone `summarize` also creates validated Kanban cards by default; use `--no-kanban` for a report only. Use `--summarizer transport:model` only when the user names a synthesizer; otherwise keep `codex:gpt-6-astra`. Pass `--name` when an explicit stable artifact name is needed.

Summarize successful reports from partial runs and identify missing reviewers. When the synthesis prompt requests work items, require complete cards under exact `### kanban/pending/<filename>.md` headings for core/shared work or `### plugins/<product>/kanban/pending/<filename>.md` headings for work owned by an initialized product submodule. Each card needs a title heading and a `**Source:**` line. The trusted runner validates and atomically creates cards in their owning trackers after synthesis; providers remain read-only. A card with a complete `**Title:**` field is normalized to a title heading while the raw response remains archived. If proposed priorities collide, the runner assigns the lowest free priorities within each pending lane and rewrites intra-consensus paths before publishing. Return the summary path, its input list, and created work-item paths.

The runner archives the synthesis response as `response.md` before materializing cards. If synthesis completed but publishing failed after the provider returned (for example, while validating work items), recover its final answer from the persisted Codex telemetry instead of paying for another model run:

```text
py .agents/skills/draxul-review/scripts/review.py recover-summary --prompt-file <synthesis-prompt> --run <review-run-id> --codex-session-file <rollout.jsonl>
```

To materialize cards from a validated consensus produced before this behavior was available:

```text
py .agents/skills/draxul-review/scripts/review.py materialize --summary-file <consensus.md>
```

## Handle failures

- Treat a nonzero runner exit as a partial or failed run; inspect its printed run path and manifest.
- Keep provider read-only/plan modes on normal calls. On native Windows, if Codex's read-only sandbox cannot create child processes with error 1312, the runner may retry without the broken OS sandbox only inside its disposable review workspace. It must preserve the original total timeout, retain the fixed review-only contract, omit source-checkout paths from review inputs, and record the fallback in the manifest.
- For authentication or connectivity failures, invoke `$draxul-preflight` and present its remediation.
- Keep temporary inline prompt files only until the runner has copied `prompt.md` into the run archive.
