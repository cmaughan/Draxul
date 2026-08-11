# Launch the macOS bundle executable from store_logs.sh

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

`scripts/store_logs.sh` invokes nonexistent `build/draxul` instead of the executable inside the macOS application bundle.

**Trigger:** Run the log-storage helper after a normal macOS build.

## Investigation

- [ ] Confirm the script’s supported platforms and documented invocation.
- [ ] Compare its path resolution with other macOS helper scripts.
- [ ] Verify paths containing spaces are handled safely.

## Fix strategy

- [ ] Resolve `build/draxul.app/Contents/MacOS/draxul` on macOS.
- [ ] Add a clear executable-not-found diagnostic before launch.
- [ ] Keep output redirection and caller-supplied log paths intact.

## Acceptance criteria

- [ ] The helper launches a standard macOS build and writes the requested log file.
- [ ] Missing builds fail with a clear actionable message.
- [ ] Paths containing spaces work correctly.
