# Require review gaps to be investigated before completion

**Summary:** Make reviewers finish explicit investigation assignments and revisit skipped areas so a useful partial report is not mistaken for a complete review.

**Priority:** P1
**Source:** `plans/prompts/review_bugs.md`; `.agents/skills/draxul-review/SKILL.md`.

- [x] Update all four review prompts with owned scope checklists, bounded concurrent assignments, meaningful investigation requirements, gap-closing passes, and honest stopping rules.
- [x] Update all four consensus prompts to report claim verification separately from combined investigation coverage and runtime validation.
- [x] Preserve review-type evidence standards, immutable source inputs, read-only execution, plain-English summaries, and root/descendant tracker deduplication; align skill and feature documentation.
- [x] Validate prompt behaviour with an independent scenario-based check, run the review-runner aggregate and skill validator, and record same-cache smoke evidence or a separately tracked application limitation.

Scope: prompt/instruction changes only. No provider review launch, model changes, runner status-schema changes, application fixes, or edits to historical review archives. Existing dirty Flashcards work is unrelated and must be preserved. Completion means the new instructions are implemented and checked, not that a new full-repository review has demonstrated exhaustive coverage.

Implemented 2026-10-02: all eight active review/consensus prompts remain self-contained for copying into private provider workspaces. Review assignments now come from the source inventory, include every initialized product/descendant, and separate platform/backend paths. Four workers is a concurrent ceiling within host limits, not a total-assignment quota. Workers' omissions require reassignment or coordinator investigation; an explicit cross-component pass follows. Complete/partial static coverage and unperformed runtime validation are reported separately from verified findings and provider execution success. Existing claim evidence and plain-English card rules are preserved. The bug consensus no longer hard-codes five product names, avoiding omission of newly initialized products such as Flashcards. The review skill and `docs/features.md` describe these prompt requirements without claiming manifest-level enforcement.

Validation:

- Independent read-only scenario evaluation covered six cases: strong findings with skipped Metal code, unavailable delegation, fully dispositioned claims with untouched discovery/CLI, static performance evidence without timings, out-of-focus defects, and an actual timeout with unfinished scope. All six followed the intended completion/evidence rules; no contradictory wording was found. This is an instruction-level check, not a live full-repository coverage demonstration.
- `py tests/review_skill_tests.py`: all 47 aggregate tests passed in 26.538 seconds, including mocked provider workflows and real Repomix packing. No paid provider review was launched.
- Skill validator passed with `uv run --no-project --with pyyaml python -X utf8 C:/Users/cmaughan/.codex/skills/.system/skill-creator/scripts/quick_validate.py .agents/skills/draxul-review`. Three earlier environment attempts failed (PyYAML absent in default/bundled Python, then Windows default decoding); isolated uv dependency resolution and explicit UTF-8 resolved these without project/global Python changes. Successful command took approximately 1.5 seconds including the adjacent diff check.
- `py do.py smoke --skip-build`: one existing Ninja Debug startup smoke passed, exit 0. No configure/compile was performed; elapsed wall-clock duration was not separately captured. This does not validate concurrent application edits from other work.
- Scoped `git diff --check` passed. No wording-matching regression tests were added. No C++ aggregate, render snapshots, Release build or remote CI was run for this prompt-only change. Other concurrent source changes and submodule work were left untouched.
