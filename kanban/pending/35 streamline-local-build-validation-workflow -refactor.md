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

## Test-integrity checkpoint (2026-08-18)

The normal core aggregate now runs the inexpensive mpack fuzz corpus instead of
reporting 34 opt-in skips, and the hidden 10,000-input VT fuzz case is explicitly
excluded rather than counted as skipped coverage. Vacuous literal/`REQUIRE(true)`
tests were removed, while startup rollback now asserts the real application error.

Five meaningful server/remote-terminal regressions were changed to exercise the
same production degradation, resync, recovery, takeover, and convergence paths with
lower injected test budgets or observable retry counts. On the Windows Debug/Ninja
cache, their combined focused runtime fell from about 70 seconds to 8.5 seconds; the
core aggregate's slow shard fell from 131.2 seconds to 73.9 seconds. The complete
nine-job core CTest gate passed in 73.9 seconds, followed by a 5.6-second same-cache
smoke.

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
- [x] Specify how a timed-out caller can recover the actual build result instead of
      abandoning still-running MSBuild/Ninja descendants.

## Build serialization and process ownership

- [x] Prevent two repository build commands from writing the same configuration tree
      concurrently, with a clear message identifying the active build and how to wait
      for or inspect it.
- [x] Ensure the wrapper owns and waits for the complete compiler/linker process tree,
      or records a durable result/log that a later invocation can collect.
- [x] Preserve compiler parallelism inside one build while prohibiting competing builds
      against the same object directory.
- [x] Detect stale build ownership safely without terminating unrelated CMake, MSBuild,
      Ninja, compiler, or linker processes.
- [x] Keep build logs concise on success and retain complete diagnostics on failure.

## Focused validation

- [x] Include each new focused executable in both its aggregate target dependencies
      and `do.py` scope-selection patterns, with runner selection coverage.
      `_test_scope_selection()` currently enumerates exact patterns; CTest
      registration alone does not include a new suite. Coordinate the proposed
      Rezonality project/runtime/audio and MegaCity parser suites from the accepted
      refactor cards, preserving the Catch/label selection and zero-match checks below.
- [x] Add a supported command for building the smallest owning test target and running
      a Catch2 name/tag or CTest label without rebuilding the complete product closure
      unnecessarily.
- [x] Validate Catch2 filter composition before launching; treat a zero-test match as a
      clear selection error and print the correctly escaped command.
- [x] Support repeat-with-new-seed for timing/concurrency integration tests without
      repeating configure or unrelated builds.
- [x] Reuse the integration-first policy from `CLAUDE.md`: focused vertical tests during
      iteration, then one final full-suite gate; do not replace useful integration
      coverage with low-value helper unit tests.
- [x] Print a compact end-of-run summary containing targets built, tests selected,
      passes/failures, durations, random seeds, and paths to retained logs.

## Live-server and helper safety

### Local process-ownership and focused-selection checkpoint (2026-09-21)

`do.py` now starts supported build, test, smoke, and render-check commands in an
owned process group. Timeout or keyboard cancellation stops that group; serialized
build cancellation also writes an `interrupted` result with exit code 130 before
releasing the build-tree lock. Windows `taskkill /T` and POSIX process-group cleanup
are covered by platform-specific command-construction tests.

`do.py test --target <draxul-test-*> [--catch <filter>]` builds one Catch2 target,
lists the matching cases before execution, rejects a zero-match filter with its
shell-escaped command, and supports `--repeat N [--seed N]` without rebuilding.
Aggregate CTest selections are also inventoried before execution, and both paths
print target, selection, result, duration, and seed/result-path summaries. Live
Windows helper preflight and cross-platform timing remain open below.

### Single final-tier checkpoint (2026-09-22)

`do.py validate` holds the selected build-tree lease across one app/full-test build,
bounded smoke, relevant deterministic render comparisons, and the complete unit CTest
inventory. The default render set is the five core comparisons available on the host;
repeated `--render` selections replace it and `--no-render` is explicit. Successful
step logs are discarded after a one-line result, while complete failed-step logs stay
under the selected build tree. The compact final summary reports built targets, CTest
selection count, render names, pass count, durations, seed policy, failure categories,
and retained log paths. Python coverage exercises successful construction, retained
failures, missing-command environment failures, and an injected Windows command seam.

### Windows live-helper preflight checkpoint (2026-09-22)

`do.py test` and `do.py validate` now inspect the default live server after their
selected cache is built. The preflight resolves the published PID to its actual
process image and mirrors the production helper-refresh size/timestamp check. It
continues without mutation for an unchanged same-cache helper and for a server owned
by another build cache. If the selected cache's live helper is stale, it exits as a
validation-environment failure before starting tests and reports the exact PID,
runtime directory, process image, attached-client count, checkpoint health/error, and
an explicit exact-runtime shutdown command. It never stops a server itself.

The detached-server process integration fixture already stages `draxul.exe` beneath
its unique temporary runtime on Windows, which in turn gives that test a private
sibling `draxul-server.exe`. A focused live run proved the process cases can launch
and replace those private helpers while the unrelated default server remains alive.
Python workflow coverage fixes the compatible, unrelated, and stale same-cache
decisions and proves a blocked preflight prevents CTest inventory/execution.

- [x] Preflight whether a running Windows Draxul server holds a helper binary that the
      requested build/test must replace.
- [x] If replacement is required, report the exact server PID, runtime directory,
      attached-client count, and checkpoint health before requesting shutdown.
- [x] Avoid stopping a compatible live server when the helper binary is unchanged.
- [x] Make isolated server integration tests use a helper location or launch strategy
      that cannot be blocked by an unrelated default-runtime server where practical.
- [x] Verify cleanup leaves no isolated server, compiler, linker, test, or render process
      running after success, failure, cancellation, or timeout.

## Integration-first validation

- [x] Exercise two attempted concurrent builds against one tree and prove the second
      waits or exits cleanly without object-file corruption or permission errors.
- [x] Exercise caller timeout/cancellation and prove the build is either cancelled as a
      complete process tree or remains discoverable with a collectable final result.
- [x] Run the focused workflow for core, app, server/RPC, and render-affecting changes.
- [x] Run with a compatible live server and with a stale helper-holding server; prove the
      preflight chooses the safe path in each case.
- [x] Verify Windows multi-config and macOS single-config command construction and
      process cleanup.
- [x] Verify the final tier performs one application/test build, smoke, relevant render
      scenarios, and full CTest without redundant reconfiguration.

## Documentation

- [x] Update `CLAUDE.md` build and validation guidance to name the streamlined commands
      and the fast-versus-final workflow.
- [x] Update `do.py --help`, wrapper help, and `docs/features.md` if the delivered runner
      becomes a supported developer-facing capability.
- [x] Document recovery from an interrupted build and from a live server-helper lock.

## Acceptance criteria

- [x] A typical RPC/input feature iteration needs one focused build and one focused
      integration-test command per code revision, followed by one final validation
      command.
- [x] No supported workflow can accidentally overlap builds in the same output tree.
- [x] Timeouts and cancellations leave either no descendants or a visible, collectable
      build result; agents do not need to guess whether compilation is still running.
- [x] The final validation command cannot fail merely because an unrelated compatible
      Draxul server is running, and handles an incompatible helper lock explicitly.
- [x] Zero-test filters fail early with actionable syntax instead of looking like a test
      run.
- [x] Final output clearly distinguishes build failures, product-test failures,
      snapshot failures, and validation-environment failures.
- [x] Windows Release build, focused integration tests, smoke, render snapshots, and
      full CTest pass; macOS command construction and CI coverage remain valid.

## macOS final-tier evidence (2026-09-22)

`CCACHE_DIR=/tmp/draxul-ccache python3 do.py validate debug` acquired one build-tree
lease and completed 8/8 steps in 130.49 seconds: one `draxul` + `draxul-tests` build
(11.75s), smoke (6.60s), the five default core Metal comparisons, and all 45 selected
unit CTest entries (103.72s). Every snapshot passed, including an exact NanoVG match.
No failure log was retained because every step passed. The live Windows helper
preflight and two-platform timing matrix remain explicitly open above.

## Windows helper-preflight evidence (2026-09-22)

- The complete `tests/do_py_tests.py` workflow suite passed 84 tests (one
  platform skip) in 1.41 seconds. Its focused 24-test workflow selection includes
  compatible, unrelated-cache, stale-helper, blocked-before-CTest, and
  final-validation seams.
- With the user's existing Debug default server still running as PID `69092`, the
  same-cache Debug preflight reported the helper unchanged and continued. The Release
  preflight resolved that same PID to the Debug helper, classified it as unrelated to
  the Release cache, and continued. The server was status-probed only and left running.
- `py do.py test debug --target draxul-test-core --catch "[server][process]"`
  passed 13 cases / 389 assertions in 24.12 seconds while PID `69092` remained alive.
  This includes the private-helper detached-server process boundary.
- `py do.py test debug --target draxul-test-app --catch "[app]"` passed 20 cases /
  209 assertions in 0.22 seconds. Its required configure/generate refresh took
  92.7 / 1.5 seconds.
- `py do.py test debug --target draxul-test-render-contracts --catch
  "[render-contracts]"` passed 1 case / 1 assertion in 0.03 seconds.
- The final Release product aggregate passed all 48 selected CTest entries in
  119.47 seconds, followed by a same-cache `py do.py smoke release --skip-build`
  pass. The workflow unit test that mocks the build and CTest layers now also
  mocks the Windows helper preflight, so it cannot inspect or depend on a real
  developer server.
- A private server at
  `build-ninja-debug/validation/card35-helper-preflight-43aac5e2bf644a289cd348aa77fc74ba/runtime`
  ran as PID `58920`. After changing only its private source-app timestamp, preflight
  refused the stale held helper and reported PID `58920`, that exact runtime, zero
  attached clients, checkpoint state `pending`, and the exact shutdown command. The
  isolated server then shut down gracefully and PID `58920` was confirmed gone.

The only remaining card checkbox is the complete two-platform timing matrix above.
Windows configure/generate and representative focused timings are now recorded here;
the already-recorded macOS final-tier run provides aggregate build/smoke/render/CTest
timings, but it does not yet separate configure, compile, and link costs by generator.
