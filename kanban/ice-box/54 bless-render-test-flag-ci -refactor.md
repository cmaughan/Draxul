# 54 Keep render-test commands out of production packages

**Type:** refactor
**Priority:** 54
**Raised by:** Claude

## Remaining problem

The render-test command line is already compiled as one unit behind
`DRAXUL_ENABLE_RENDER_TESTS`; a second bless-only compile definition would
duplicate that policy. The CMake option defaults to `OFF`, and it guards the
parsed fields, argument branches, render-scenario loading, capture, comparison,
export, and blessing paths.

The unresolved boundary is the distribution build. Every checked-in developer
preset, including `release`, `win-ninja-release`, and `mac-release`, explicitly
sets `DRAXUL_ENABLE_RENDER_TESTS=ON`. `do.py deploy` builds through that Release
preset and may reuse its cache before staging the same executable. Consequently
the deploy package currently includes `--render-test`,
`--export-render-test`, `--show-render-test-window`, and
`--bless-render-test`.

## Implementation plan

- [ ] Define a dedicated production-package configure policy with
      `DRAXUL_ENABLE_RENDER_TESTS=OFF` on both Windows and macOS. Keep the
      existing development/CI presets enabled so compare and bless workflows
      continue to work.
- [ ] Make `do.py deploy` use that policy in an isolated or freshly verified
      cache. It must never reuse a Release cache configured with render tests
      enabled.
- [ ] Add a packaging regression test that inspects the deploy configuration
      and runs the staged executable's argument parser, proving all four
      render-test-only options are rejected as unknown in the production
      package, including `--bless-render-test`.
- [ ] Document that deterministic render compare/bless commands are available
      in developer/CI builds and omitted from production packages.
- [ ] Validate the package-policy tests on Windows and macOS and run the normal
      deploy smoke for each platform.

## Acceptance criteria

- [ ] Development and CI builds retain the current render compare, export,
      visible-window, and bless workflows under `DRAXUL_ENABLE_RENDER_TESTS`.
- [ ] `do.py deploy` cannot stage an executable built with
      `DRAXUL_ENABLE_RENDER_TESTS=ON`.
- [ ] A packaged executable rejects `--bless-render-test` and the other
      render-test-only options without modifying reference images.
- [ ] No separate `DRAXUL_ENABLE_RENDER_BLESS` option or nested compile-time
      branch is introduced.

## Static evidence

- Root `CMakeLists.txt` declares `DRAXUL_ENABLE_RENDER_TESTS` with default
  `OFF` and exports the definition to the application only when enabled.
- `app/cli_args.h`, `app/cli_args.cpp`, and `app/main.cpp` already guard the
  complete render-test CLI and execution path with that definition.
- `CMakePresets.json` explicitly enables the option in all normal Release
  presets, while `do.py::cmd_deploy` calls the shared Release
  configure/build path and stages its resulting binary. That is the only policy
  gap retained by this card.
