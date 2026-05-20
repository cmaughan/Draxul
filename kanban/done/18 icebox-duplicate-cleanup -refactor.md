# 18 icebox-duplicate-cleanup -refactor

**Type:** refactor
**Priority:** 18 (hygiene)
**Source:** Claude review (review-latest.claude.md); GPT review (review-latest.gpt.md)

## Problem

The `plans/work-items-icebox/` directory contains duplicate entries for at least three items:
- `20 searchable-scrollback -feature.md` and `20 searchable-scrollback -feature 1.md`
- `21 per-pane-env-overrides -feature.md` and `21 per-pane-env-overrides -feature 1.md`
- `22 bracketed-paste-confirmation -feature.md` and `22 bracketed-paste-confirmation -feature 1.md`

Both Claude and GPT flag this. It confuses automated tooling, creates divergence risk (which copy is authoritative?), and misleads reviewers doing icebox triage.

Additionally, GPT flagged that `plans/prompts/consensus_review.md` references `plans/reviews/_latest_` and `review-concensus.md` (note: misspelling), neither of which match the current repository layout. This should be updated to reference the real paths.

## Acceptance Criteria

- [x] For each duplicate icebox item pair: compare the two files and determine which is more complete/accurate.
- [x] Delete the less-complete copy. If they are identical, delete the ` 1` suffix version.
- [x] Update `plans/prompts/consensus_review.md` to reference correct paths (`plans/reviews/` and `review-consensus.md`).
- [x] Verify no `do.py` commands or other scripts reference the deleted filenames.
- [x] Commit the cleanup.

## Implementation Notes

- Read both copies of each duplicate before deleting. If they have diverged, merge the content into the canonical copy first.
- This is safe to do with a sub-agent: list both files, diff them, pick the canonical copy, delete the other.

## Interdependencies

- No blockers. Completely independent hygiene item.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
