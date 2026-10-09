# Explain review cards in plain English

**Summary:** Give every review card a short explanation of the change and its purpose so readers can understand it before studying the technical details.

**Source:** `.agents/skills/draxul-review/SKILL.md`, `plans/prompts/`, root and product pending boards.
**Priority:** P2

- [x] Require an opening summary in feature, bug, refactor, and performance review and consensus prompts, plus the skill and runtime synthesis instructions.
- [x] Reject generated cards with missing, empty, or misplaced summaries before publishing any cards; preserve archived responses for repair.
- [x] Add individually written summaries to all 92 existing pending cards across the root and five initialized product boards without changing their evidence or checklists.
- [x] Run review-runner regression tests, skill validation, a recursive card-preservation audit, and a same-cache startup smoke.

The validator checks the presence and placement of the explanation; the prompts and human inspection govern clarity and factual accuracy. Historical reports and done/ice-box cards remain unchanged.

Validation on Windows, 2026-09-30:

- `py -m unittest tests.review_skill_tests`: all 45 tests passed in 20.3 seconds, including whole-batch rejection before any file is created and preservation of summaries, metadata, and checklist progress.
- Recursive preservation audit: all 92 original pending cards differ only by their opening summary (root 57, MegaCity 10 3, Rezonality 8, SatView 8, ScoreView 6).
- Skill Creator `quick_validate.py`: passed using `uv run --with pyyaml python -X utf8`; the system Python lacked PyYAML and the default Windows encoding could not decode existing Unicode punctuation. Validation took about one second after those environment-only retries.
- `py do.py smoke debug --skip-build`: passed within its 30-second bound using the existing Debug cache.
- Root and all product `git diff --check` checks passed. No configure, compilation, native product tests, render snapshots, Release build, remote CI, or paid review-provider calls were needed for this prompt/documentation and Python publisher change; the full review-runner suite was the scope-appropriate aggregate.
