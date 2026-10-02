---
name: draxul-review
description: Run independent Draxul repository reviews through multiple AI companies, write archived and latest Markdown reports, or synthesize selected reviews with a caller-supplied prompt. Use for multi-model code review, feature/bug/refactor/optimization review panels, consensus, comparison, or review summarization.
---

# Draxul multi-AI review

Use `scripts/review.py` for every provider call. Do not call reviewer CLIs directly.

## Generate reviews

1. Resolve the requested prompt file relative to the repository root. If the user supplied inline text, preserve it exactly in a temporary UTF-8 prompt file.
2. Choose reviewers:
   - No selection: use the two-model panel: OpenAI `gpt-6.1-sol` (GPT-6.1 Sol) and Anthropic `claude-opus-5-5` (Claude Opus 5.5), both at high effort.
   - “All”: pass `--all` to include every healthy configured company.
   - Named reviewers: pass repeatable `--reviewer transport:model` values. Supported transports are `codex`, `claude`, `google`, `agy`, `gemini`, and `grok`.
3. Run from the repository root:

```text
py .agents/skills/draxul-review/scripts/review.py review --prompt-file <prompt> [--reviewer <transport:model> ... | --all] [--name <artifact-name>]
```

4. Arrange a status check every 10 minutes as described below. The runner saves each reviewer report and diagnostics immediately, updates the running manifest, then automatically produces consensus from that exact run. Report the immutable run directory, successful/failed reviewers, fallbacks, latest files, archived Repomix input, consensus path, and any created Kanban cards. Use `--no-consensus` only when the user requests independent reviews without synthesis.

Codex and Claude calls explicitly use **high reasoning effort**, including synthesis. Codex review/synthesis calls enable subagents with a maximum of four concurrent workers and explicitly set worker defaults to the selected model at high effort. Preflight disables delegation. Other optional transports retain their provider effort defaults.

The feature, bug, refactor, and optimization prompts require an owned Markdown checklist derived from the source inventory, splitting large subsystems and each initialized product into bounded responsibilities. Read-only delegation is capped at four simultaneous workers within the host's actual limit, not four assignments total; reuse or replace workers as work finishes, without nested delegation. Workers share the same packed source and review-only restrictions. If delegation is unavailable, review sequentially and disclose the limitation. Review implementations, callers, dependencies, tests and separate platform/backend paths using each review type's evidence rules. The coordinator verifies claims, reassigns skipped/skimmed/partial responsibilities in gap-closing passes, and performs a separate cross-component pass before declaring static coverage complete. No finding quota applies, and examined areas with no supported findings remain valid completed assignments.

Reports must distinguish static investigation coverage from runtime validation and list concrete paths/contracts for each responsibility, its owner, evidence and unfinished work. A partial report must name the actual execution limit or admit unfinished investigation; repository size alone is not an observed limit. Packed-source reviews remain static-only: inspected tests and archived measurements are not freshly executed validation. Consensus separately reports finding-verification status and combined static coverage, preserving original gaps unless additional investigation actually closes them. Unresolved leads and investigation gaps are not speculative Kanban tasks. Provider/runner `complete` status means execution finished, not that investigation coverage was complete; this distinction is reported in Markdown, not currently enforced by the manifest schema.

Reviews use **one Repomix file and one coordinating review run per provider**, with no source segments, batches, or coverage receipts. Install the `repomix` CLI on PATH before running a review. The runner first freezes one source snapshot shared by the panel, recursively including initialized Git submodules (including dirty and non-ignored untracked files), and fails if any submodule is missing. Git metadata, ignored files, `plans/reviews/` archives, and previous `repomix-output.*` files are excluded.

The runner invokes Repomix once on that snapshot to generate `repomix-output.xml`, with the directory tree, original paths, line numbers, comments, and full source text (no compression or splitting). Repomix's binary and security filtering remain enabled; its diagnostics are saved in `logs/repomix.log`. The exact generated file is archived with size and SHA-256 provenance in `input.json` and the review manifest. Each reviewer gets a private copy of that same file plus `REVIEW_PROMPT.md`, and uses the packed file as its sole source input. Reviewers can read and search across embedded file sections and delegate bounded investigations within that run. They must state context or scope limitations honestly; one run does not guarantee exhaustive review of a large repository.

Generate and inspect the packed input without invoking providers:

```text
py .agents/skills/draxul-review/scripts/review.py review --prompt-file <prompt> --plan-only
```

The runner preflights providers after packing, runs their one-shot reviews concurrently, and owns all report writes. The default timeout is 90 minutes **per reviewer and synthesis**, with a separate preparation timeout of the same duration for Repomix. Each report is published as its reviewer finishes; a slow or failed peer cannot keep completed reports only in memory. After the panel finishes, successful reports are synthesized automatically by `codex:gpt-6.1-sol` at high effort. Partial panels produce explicitly partial consensus; no successful reports means no synthesis. Consensus failure preserves the reviews and returns a nonzero exit status. Persist real Codex, Claude, and Grok review sessions in their normal provider stores so TokenFu can retain tool-call and token telemetry; keep nonce-only preflight sessions ephemeral. Start fresh sessions and keep cross-session memory disabled.

## Monitor through completion

For every launched review, arrange a recurring 10-minute status check in the calling task using the host's supported scheduler (a Codex thread heartbeat when available). Reuse an existing monitor for the same run rather than creating duplicates. Record the exact run directory and monitor identifier so follow-ups inspect the correct run. If scheduling is unavailable, disclose that and keep checking in the active session; do not claim a future check is scheduled.

At each check, read the run's `manifest.json` and any linked consensus manifest, and verify matching process IDs before declaring a still-running or unexpectedly stopped process. Report each reviewer's state and elapsed time, saved report paths when available, the consensus state, and any error. Report every scheduled check even when work remains in progress; do not invent a completion percentage or restart providers automatically.

Continue until both the review panel and automatic consensus have reached terminal states. Review completion alone is not the stopping condition. Verify the summary and all listed Kanban files exist. If the user requested merging tasks, reconcile accepted findings against current root/product tracker lanes, merge supported refinements into the matching existing cards while preserving checked tasks and unrelated content, and remove only newly generated duplicate cards after preserving their useful content and repairing references. Never repeat a review merely to merge tasks.

Report final successes/failures, consensus and report paths, and created/merged work items, then disable or delete the monitor. If the runner stops unexpectedly or fails without starting consensus, report the failure and stop that monitor rather than polling a stale running manifest indefinitely.

## Default consensus and Kanban options

### Plain-English card summaries

Every proposed, created, or revised card must put `**Summary:**` immediately below its title heading, before priority, source, or other metadata. Write one short sentence explaining what will change and why it is needed for someone who has not read the code. Use everyday language; avoid class names, file paths, acronyms, and unexplained technical terms. Describe the problem and intended benefit without unsupported claims or invented speedups. Keep technical evidence and implementation details below it.

Apply this to all review types, root and product cards, and cards revised during consensus merging; preserve their evidence and checklist progress. The runner rejects missing, empty, or misplaced summaries before publishing cards. It checks structure, not whether the explanation is accurate or easy to understand; inspect those qualities when reviewing the result.

For example: `**Summary:** Avoid repeatedly checking the same text during redraws, reducing unnecessary work when updating large terminal windows.`

### Run options

- `review` produces a consensus report and validated Kanban cards by default.
- Use `--no-kanban` for a consensus report without creating cards; `--kanban` explicitly enables the default behavior.
- Use `--no-consensus` for independent reports only. It skips both synthesis and card creation and cannot be combined with `--consensus-prompt`.
- The default consensus prompt is `plans/prompts/consensus_<review-prompt-stem>.md` (for example `consensus_review_bugs.md`); a general reconciliation prompt is used when no matching saved prompt exists.
- `--consensus-prompt <path>` overrides the saved prompt. `--summarizer transport:model` overrides GPT-6.1 Sol only when the user names a synthesizer.
- Consensus always uses the current immutable review run, not mutable latest-file globs. Its state and summary-run link are recorded in the review manifest, separately from reviewer completion status.
- `--plan-only` generates the Repomix input without invoking reviewers or consensus.

## Summarize reviews

Resolve exactly which reviews the user selected. Prefer an immutable run id when available.

```text
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --run <run-id>
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --input <review.md> [--input <review.md> ...]
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --glob <pattern>
```

Standalone `summarize` also creates validated Kanban cards by default; use `--no-kanban` for a report only. Use `--summarizer transport:model` only when the user names a synthesizer; otherwise keep `codex:gpt-6.1-sol`. Pass `--name` when an explicit stable artifact name is needed.

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

- Claude review/synthesis subprocesses set `CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS=0` so delegated workers can finish instead of hitting Claude print mode's ten-minute background wait ceiling. The runner's outer `--timeout` remains in force; user-wide environment settings are not changed. A background-task termination diagnostic is a failure even with exit code zero. Plain progress messages are not accepted as Markdown reports; inspect saved findings and coverage before treating a structurally valid report as substantive.
- If the user authorizes retrying only a failed reviewer while a peer run is still active, launch a separately named single-reviewer run with `--no-consensus`. Preserve the original run. After both reports are available, use `summarize` with exact `--input` paths for the successful peer and replacement report; exclude the failed/progress-only report. Monitor both runs and the replacement consensus through completion, and identify any original automatic consensus as superseded rather than merging its invalid input into the replacement.
- Treat a nonzero runner exit as a partial or failed run; inspect its printed run path and manifest.
- Keep provider read-only/plan modes on normal calls. On native Windows, if Codex's read-only sandbox cannot create child processes with error 1312, the runner may retry without the broken OS sandbox only inside its disposable review workspace. It must preserve the original total timeout, retain the fixed review-only contract, omit source-checkout paths from review inputs, and record the fallback in the manifest.
- For authentication or connectivity failures, invoke `$draxul-preflight` and present its remediation.
- Keep temporary inline prompt files only until the runner has copied `prompt.md` into the run archive.
