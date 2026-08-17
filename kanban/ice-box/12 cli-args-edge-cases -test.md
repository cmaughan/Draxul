# CLI argument edge cases and CWD preservation

**Type:** test
**Priority:** 12

`app/cli_args.cpp` and `tests/cli_args_tests.cpp` already provide the parser and broad
host/option coverage. Retain only the missing edge cases.

- [ ] Cover `--log-file`, every `--log-level`, missing values, and unknown options.
- [ ] Assert parsing and startup option resolution do not mutate the parent process CWD.
- [ ] Preserve Windows/macOS path and quoting behavior.
- [ ] Run focused CLI tests and same-cache smoke.
