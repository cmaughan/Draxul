# 05 windows-cmdline-quoting -bug

**Priority:** MEDIUM
**Type:** Bug (latent, Windows-only)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-nvim/src/nvim_process.cpp` (or related Windows process spawn code) contains a hand-rolled `quote_windows_arg()` function for constructing the `lpCommandLine` string passed to `CreateProcessA`. Hand-rolled Windows argument quoting is inherently fragile: trailing backslashes before a closing quote, and embedded double-quotes in arguments, are common edge cases that break the MSVC `CommandLineToArgvW` parsing rules. This is a latent correctness bug and a potential argument-injection surface if any path component is user-controlled (e.g., from a file-drop or config path).

---

## Fix Plan

- [x] Read the Windows process spawn code (`libs/draxul-nvim/src/nvim_process.cpp` or adjacent files) and locate `quote_windows_arg()`.
- [x] Evaluate whether the current quoting correctly handles:
  - Paths with spaces
  - Paths ending in backslash (e.g., `C:\foo\`)
  - Paths with embedded double-quotes (unlikely but must not corrupt the command line)
- [x] Options for fix:
  - **Preferred**: Switch to `CreateProcessW` with a properly split `lpApplicationName` + `lpCommandLine`, where the nvim binary path goes in `lpApplicationName` (never quoted) and arguments are individually quoted per the MSVC spec.
  - **Alternatively**: Replace `quote_windows_arg()` with a well-tested implementation following the documented escaping rules (backslash-runs before quotes doubled, trailing backslashes before closing quote doubled).
- [x] Add a unit test covering the quoting edge cases (spaces, trailing backslash, embedded quotes) — see **12-test** for the file-drop variant.
- [x] Build on Windows and run smoke test.

---

## Acceptance

- Nvim paths containing spaces (e.g., `C:\Program Files\nvim\nvim.exe`) launch correctly.
- Paths with trailing backslashes do not corrupt the command line.
- No argument injection is possible through user-supplied path strings.

---

## Interdependencies

- Windows-only. Irrelevant on macOS builds.
- No upstream dependencies.
