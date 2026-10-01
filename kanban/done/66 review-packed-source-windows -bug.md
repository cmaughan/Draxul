# Include nested source files in Windows reviews

**Summary:** Ensure code reviews receive files inside folders and plugins so they can find bugs throughout the project instead of reviewing only top-level files.

**Priority:** P1
**Source:** `.agents/skills/draxul-review/scripts/review.py`, failed run `20261001T142232Z-review-bugs-be505b20`.

- [x] Trace the source-packing gap: 1,970 supplied paths became only 19 root files. Repomix's stdin path conversion produces Windows backslashes that fail nested glob matching.
- [x] Supply the frozen source allowlist through Repomix's JSON config with forward-slash paths; retain binary/security filtering and exclude the synthetic Git-state helper.
- [x] Update review/preflight/consensus defaults, skill metadata and documentation to GPT-6.1 Sol; keep Opus 5.5 and High effort.
- [x] Validate the real CLI against nested core/product paths and run the review-runner test suite and skill validators.
- [x] Verify the full project pack and restart the requested bug panel with live provider preflights.

The prior build changes and their tracker cards are unrelated and must be preserved. No application code is changed by this tooling fix.

Validation, Windows 2026-10-01: all 46 review-runner tests passed in 25.33 seconds, including a real Repomix invocation that retains nested core/product files and paths with spaces while excluding the synthetic helper. Both skill validators passed (2.2 seconds combined). Live preflight passed for codex-cli 0.159.2 with GPT-6.1 Sol and Claude Code 2.1.286 with Opus 5.5. No model fallback was used.

Full review run: `plans/reviews/runs/20261001T161840Z-review-bugs-227f1d80`. Repomix received 1,971 files and retained 1,780 text files after filtering, producing 23,426,691 bytes (SHA-256 `1a6c2d298412436b5781a669164d82ec2897e2e4322f551ce16f2e3edc25fac7`). Both reviewers started; automatic consensus and card publication remain the review workflow's responsibility. Heartbeat `draxul-bug-review-status` monitors that exact run every ten minutes through consensus completion.

Application validation is separate from this tooling-only fix: the same-cache Debug smoke again timed out at 30 seconds, owned by [65 windows-validation-timing -test.md](../pending/65%20windows-validation-timing%20-test.md). An additional Release startup command triggered dependency reconfiguration under the updated local toolchain and was cancelled before compilation/startup; no new Release result is claimed. No application source changed, so no C++ aggregate or render snapshot was repeated. Remote CI/macOS were not run.
