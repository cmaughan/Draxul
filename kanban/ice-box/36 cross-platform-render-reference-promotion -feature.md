# Cross-platform render reference promotion

**Type:** feature
**Priority:** 36
**Raised by:** Claude

## User need

A developer on Windows cannot locally produce trustworthy macOS Metal references, and vice versa. Reference updates currently require manual access to both platforms.

## Implementation plan

- [ ] Ensure CI publishes cross-platform render-test artifacts (already in place: `.github/workflows/build.yml` runs on push/PR and uploads both platforms' render outputs) and define CI artifact metadata: commit SHA, platform, build configuration, manifest hash, scenario name, image hash, and comparison report.
- [ ] Add `py do.py bless-from-ci <run-id> [--platform ...] [--scenario ...]` using `gh run download` or the GitHub API.
- [ ] Verify the run belongs to the same repository/commit (or require an explicit override), succeeded, and used a compatible manifest.
- [ ] Stage downloads in a temporary directory and show old/new hashes plus diff summary before replacing references.
- [ ] Refuse unknown/orphan scenarios and never overwrite the local platform's unrelated references.
- [ ] Replace selected files atomically and print the exact validation command to run.
- [ ] Document the two-platform review/bless workflow.

## Tests and acceptance

- [ ] Unit-test metadata validation, wrong commit/platform, partial artifacts, hash mismatch, and selective promotion with a fake `gh` runner.
- [ ] End-to-end test against a fixture artifact archive.
- [ ] A Windows developer can promote macOS outputs and vice versa without manual archive surgery.
- [ ] No reference changes occur without an explicit scenario/platform selection or confirmation.

## Dependencies and parallelism

Depends on CI publishing cross-platform render-test artifacts — now satisfied (`.github/workflows/build.yml` runs automatically on push/PR and uploads both platforms' render outputs). Good isolated tooling sub-agent task.

<model>GPT-5 Codex</model>
