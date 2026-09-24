# Launch Windows Neovim using Unicode process APIs
**Severity:** HIGH  
**Source:** Codex #9; `libs/draxul-nvim/src/nvim_process.cpp:169`.

UTF-8 executable, argument, and working-directory strings are interpreted through the Windows ANSI code page.

**Investigation**

- [ ] Trace launch string encoding, argument quoting, and environment construction.

**Fix strategy**

- [ ] Convert launch values to UTF-16 and use `CreateProcessW` with wide startup structures and a compatible environment block.

**Acceptance criteria**

- [ ] Neovim launches with non-ASCII executable paths, working directories, and arguments on a non-UTF-8 Windows code page.
- [ ] Preserve quoting and environment behavior; run Windows core aggregate tests and same-cache smoke, with macOS regression coverage.
