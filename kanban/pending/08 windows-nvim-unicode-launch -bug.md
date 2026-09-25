# Launch Windows Neovim using Unicode process APIs
**Severity:** HIGH  
**Source:** Codex #9; `libs/draxul-nvim/src/nvim_process.cpp:169`.

UTF-8 executable, argument, and working-directory strings are interpreted through the Windows ANSI code page.

**Investigation**

- [x] Trace launch string encoding, argument quoting, and environment construction.

**Fix strategy**

- [x] Convert launch values to UTF-16 and use `CreateProcessW` with wide startup structures and a compatible environment block.

**Acceptance criteria**

- [x] Windows fake-child integration test preserves non-ASCII executable/cwd/argument values, quoting, and inherited Unicode environment.
- [x] Run the Windows core/product aggregate and a same-cache startup smoke with a sufficient bound.
- [x] Verify an actual Neovim binary in non-ASCII executable and working-directory paths on a non-UTF-8 Windows code page.
- [ ] Run macOS launch regression coverage.

**Implementation notes:** The previous `CreateProcessA` call took UTF-8 path,
arguments, and working directory through the active ANSI code page, and
`GetEnvironmentStringsA` similarly narrowed inherited values. Launch now
validates and converts UTF-8 inputs to UTF-16 before creating handles; it uses
the existing wide argument-quoter, `CreateProcessW`, `STARTUPINFOW`, a copied
wide inherited environment with `TERM=dumb`, and
`CREATE_UNICODE_ENVIRONMENT`. A Windows fake-child integration test copies the
executable into a Unicode directory and checks its Unicode cwd, quoted argument,
and inherited Unicode environment value. The Space-root handoff now uses
`path::u8string()` so the cwd is not narrowed before reaching that launch API.

**Validation (Windows, 2026-09-25):** `py do.py test debug --products`
passed 49/49 CTest cases in 238.19s, including the Unicode fake-child launch
test. The standard same-cache smoke exceeded its fixed 30s limit while
restoring the existing nine-pane session; the identical wrapper environment
passed with a 90s bound in approximately 45s. MacOS coverage remains to be
checked.

**Real Neovim check (Windows, 2026-09-25):** Windows `GetACP()` returned
1252. A new integration test copies `nvim.exe` and adjacent DLLs into a
Japanese-named executable directory, launches it from a Greek-named working
directory through `NvimProcess`, and confirms Neovim's `getcwd()` RPC resolves
to that directory. After adding bounded cleanup for the temporary executable,
the focused `draxul-test-nvim-transport` run passed 1 case and 6 assertions
in 0.28s. Only the macOS launch regression remains open.
