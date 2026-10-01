# Preserve source-path characters through launch

**Summary:** Read file names in their original characters so Windows can open Markdown files with non-English names.

**Priority:** 81  
**Severity:** HIGH  
**Source:** `libs/draxul-app-cli/src/cli_args.cpp`

**Evidence and trigger:** B18; UTF-8 arguments undergo narrow Windows path conversion in CLI parsing and Markdown source resolution.

- [ ] **Investigate:** Trace source and working-directory decoding and serialization through CLI and host launch.
- [ ] **Fix:** Use explicit UTF-8 boundaries while retaining native filesystem paths.
- [ ] **Acceptance:** Absolute and relative non-English source paths open on Windows with a non-UTF-8 code page; ordinary paths remain valid.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; verify the Windows path scenario.
