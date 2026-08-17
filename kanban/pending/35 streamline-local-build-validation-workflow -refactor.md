# Streamline the local build and validation workflow

**Type:** refactor
**Priority:** P1
**Raised by:** Codex implementation retrospective for `kanban/pending/34 hidden-remote-terminal-suspension -bug.md`

## Goal

Make the normal agent/developer loop fast, serialized, observable, and difficult to
misuse. A focused implementation should use the smallest relevant build and integration
tests while iterating, then run one reliable final build, smoke, render, and full-suite
gate without orphaned compiler processes or interference from a live Draxul server.

## Delivered checkpoint (2026-08-17)

Commit `b88906f0` made Debug/Ninja the normal Windows development cache, added
core-by-default and product-opt-in `do.py test` scopes with bounded parallel CTest,
made build mode explicit, added same-cache `smoke --skip-build`, and updated the
canonical guidance, README, help, and Python tests. The remaining work below is
process ownership/serialization, exact Catch/CTest selection, live-helper preflight,
and a single final validation gate.

## Evidence from the hidden-terminal suspension run

The implementation run invoked the build pipeline 11 times:

- 8 completed successfully: 7 Release builds and 1 Debug/Ninja build;
- 1 timed out at the tool boundary and completed without a captured result;
- 1 short-timeout build left descendant compiler processes running; and
- 1 subsequent build overlapped those descendants and failed with object-file
  `Permission denied` errors.

It launched 17 top-level test commands, of which one malformed Catch2 filter matched no
tests. The useful test activity included:

- 4 full CTest runs: 3 clean 24/24 passes and 1 run with 2 environment failures because
  a live Draxul server held the globally cached Windows server helper open;
- 1 targeted rerun of those 2 failed CTest entries, both passing after the exact live
  server was stopped;
- 7 focused `[suspend]` runs: 4 passes and 3 failures that exposed and then removed a
  timing-sensitive snapshot assertion;
- 1 clipboard suite, 1 lease test, and 1 long server-restart test, all passing;
- 5 smoke executions in total: 4 through CTest and 1 standalone `do.py smoke`; and
- 20 render-test executions across the 4 full suites, all passing.

The repeated focused suspension tests added value. The overlapping builds, lost result,
no-match test command, repeated full suites, Debug rebuild during a Release validation
loop, and helper-lock failures were avoidable workflow cost.

## Workflow design

- [ ] Measure configure, compile, link, focused-test, smoke, render, and full-CTest time
      on the supported Windows and macOS generators.
- [x] Define an explicit fast iteration tier and a final validation tier, including
      which source areas map to which existing CMake targets and CTest labels.
- [x] Decide whether the primary interface should extend `do.py`, `t.bat`/`t.sh`, or a
      small shared runner used by all three; keep CMake/CTest authoritative.
- [x] Make configuration and build type explicit so a Release implementation loop does
      not unexpectedly configure and rebuild the Debug/Ninja tree for smoke.
- [ ] Specify how a timed-out caller can recover the actual build result instead of
      abandoning still-running MSBuild/Ninja descendants.

## Build serialization and process ownership

- [ ] Prevent two repository build commands from writing the same configuration tree
      concurrently, with a clear message identifying the active build and how to wait
      for or inspect it.
- [ ] Ensure the wrapper owns and waits for the complete compiler/linker process tree,
      or records a durable result/log that a later invocation can collect.
- [ ] Preserve compiler parallelism inside one build while prohibiting competing builds
      against the same object directory.
- [ ] Detect stale build ownership safely without terminating unrelated CMake, MSBuild,
      Ninja, compiler, or linker processes.
- [ ] Keep build logs concise on success and retain complete diagnostics on failure.

## Focused validation

- [ ] Add a supported command for building the smallest owning test target and running
      a Catch2 name/tag or CTest label without rebuilding the complete product closure
      unnecessarily.
- [ ] Validate Catch2 filter composition before launching; treat a zero-test match as a
      clear selection error and print the correctly escaped command.
- [ ] Support repeat-with-new-seed for timing/concurrency integration tests without
      repeating configure or unrelated builds.
- [x] Reuse the integration-first policy from `CLAUDE.md`: focused vertical tests during
      iteration, then one final full-suite gate; do not replace useful integration
      coverage with low-value helper unit tests.
- [ ] Print a compact end-of-run summary containing targets built, tests selected,
      passes/failures, durations, random seeds, and paths to retained logs.

## Live-server and helper safety

- [ ] Preflight whether a running Windows Draxul server holds a helper binary that the
      requested build/test must replace.
- [ ] If replacement is required, report the exact server PID, runtime directory,
      attached-client count, and checkpoint health before requesting shutdown.
- [ ] Avoid stopping a compatible live server when the helper binary is unchanged.
- [ ] Make isolated server integration tests use a helper location or launch strategy
      that cannot be blocked by an unrelated default-runtime server where practical.
- [ ] Verify cleanup leaves no isolated server, compiler, linker, test, or render process
      running after success, failure, cancellation, or timeout.

## Integration-first validation

- [ ] Exercise two attempted concurrent builds against one tree and prove the second
      waits or exits cleanly without object-file corruption or permission errors.
- [ ] Exercise caller timeout/cancellation and prove the build is either cancelled as a
      complete process tree or remains discoverable with a collectable final result.
- [ ] Run the focused workflow for core, app, server/RPC, and render-affecting changes.
- [ ] Run with a compatible live server and with a stale helper-holding server; prove the
      preflight chooses the safe path in each case.
- [ ] Verify Windows multi-config and macOS single-config command construction and
      process cleanup.
- [ ] Verify the final tier performs one application/test build, smoke, relevant render
      scenarios, and full CTest without redundant reconfiguration.

## Documentation

- [x] Update `CLAUDE.md` build and validation guidance to name the streamlined commands
      and the fast-versus-final workflow.
- [x] Update `do.py --help`, wrapper help, and `docs/features.md` if the delivered runner
      becomes a supported developer-facing capability.
- [ ] Document recovery from an interrupted build and from a live server-helper lock.

## Acceptance criteria

- [x] A typical RPC/input feature iteration needs one focused build and one focused
      integration-test command per code revision, followed by one final validation
      command.
- [ ] No supported workflow can accidentally overlap builds in the same output tree.
- [ ] Timeouts and cancellations leave either no descendants or a visible, collectable
      build result; agents do not need to guess whether compilation is still running.
- [ ] The final validation command cannot fail merely because an unrelated compatible
      Draxul server is running, and handles an incompatible helper lock explicitly.
- [ ] Zero-test filters fail early with actionable syntax instead of looking like a test
      run.
- [ ] Final output clearly distinguishes build failures, product-test failures,
      snapshot failures, and validation-environment failures.
- [x] Windows Release build, focused integration tests, smoke, render snapshots, and
      full CTest pass; macOS command construction and CI coverage remain valid.
