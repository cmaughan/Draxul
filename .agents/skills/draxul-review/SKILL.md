---
name: draxul-review
description: Run independent Draxul repository reviews through multiple AI companies, write archived and latest Markdown reports, or synthesize selected reviews with a caller-supplied prompt. Use for multi-model code review, feature/bug/refactor review panels, consensus, comparison, or review summarization.
---

# Draxul multi-AI review

Use `scripts/review.py` for every provider call. Do not call reviewer CLIs directly.

## Generate reviews

1. Resolve the requested prompt file relative to the repository root. If the user supplied inline text, preserve it exactly in a temporary UTF-8 prompt file.
2. Choose reviewers:
   - No selection: use the default OpenAI, Anthropic, and Google panel.
   - “All”: pass `--all` to include every healthy configured company.
   - Named reviewers: pass repeatable `--reviewer transport:model` values. Supported transports are `codex`, `claude`, `google`, `agy`, `gemini`, and `grok`.
3. Run from the repository root:

```text
py .agents/skills/draxul-review/scripts/review.py review --prompt-file <prompt> [--reviewer <transport:model> ... | --all] [--name <artifact-name>]
```

4. Report the immutable run directory, successful/failed reviewers, fallbacks, and latest files. Do not summarize unless the user asked for synthesis.

The runner preflights providers, creates isolated snapshots, runs reviewers concurrently, and owns all report writes. It prints flushed provider start/completion events while work is active. The default reviewer timeout is 30 minutes. Persist real Codex, Claude, and Grok review sessions in their normal provider stores so TokenFu can retain tool-call and token telemetry; keep nonce-only preflight sessions ephemeral. Start fresh sessions and keep cross-session memory disabled.

## Summarize reviews

Resolve exactly which reviews the user selected. Prefer an immutable run id when available.

```text
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --run <run-id>
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --input <review.md> [--input <review.md> ...]
py .agents/skills/draxul-review/scripts/review.py summarize --prompt-file <synthesis-prompt> --glob <pattern>
```

Use `--summarizer transport:model` only when the user names a synthesizer; otherwise keep `codex:gpt-5.6-sol`. Pass `--name` when an explicit stable artifact name is needed.

Summarize successful reports from partial runs and identify missing reviewers. When the synthesis prompt requests `kanban/pending/` work items, require complete cards under exact `### kanban/pending/<filename>.md` headings. The trusted runner validates and atomically creates those cards after synthesis; providers remain read-only. If proposed priorities collide with the pending lane, the runner assigns the lowest free priorities in consensus order and rewrites intra-consensus paths before publishing. Return the summary path, its input list, and created work-item paths.

If synthesis completed but publishing failed after the provider returned (for example, while validating work items), recover its final answer from the persisted Codex telemetry instead of paying for another model run:

```text
py .agents/skills/draxul-review/scripts/review.py recover-summary --prompt-file <synthesis-prompt> --run <review-run-id> --codex-session-file <rollout.jsonl>
```

To materialize cards from a validated consensus produced before this behavior was available:

```text
py .agents/skills/draxul-review/scripts/review.py materialize --summary-file <consensus.md>
```

## Handle failures

- Treat a nonzero runner exit as a partial or failed run; inspect its printed run path and manifest.
- Keep provider read-only/plan modes on normal calls. On native Windows, if Codex's read-only sandbox cannot create child processes with error 1312, the runner may retry without the broken OS sandbox only inside its disposable repository snapshot. It must preserve the original total timeout, retain the fixed review-only contract, omit source-checkout paths from snapshot inputs, and record the fallback in the manifest.
- For authentication or connectivity failures, invoke `$draxul-preflight` and present its remediation.
- Keep temporary inline prompt files only until the runner has copied `prompt.md` into the run archive.
