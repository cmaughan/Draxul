# Bug: APPDATA Empty-String Produces Invalid Config Path

**Type:** bug
**Priority:** 2
**Source:** Claude review

## Problem

In `libs/draxul-config/src/app_config_io.cpp`, the code does:

```cpp
const char* appdata = std::getenv("APPDATA");
if (appdata == nullptr) { /* fallback */ }
// ... uses appdata directly
```

The null-check is present, but there is no empty-string check. If `APPDATA` is set to an empty string (unusual but possible in constrained CI environments or Docker containers), the resulting config path becomes just the filename with no directory prefix — e.g. `"config.toml"` — causing the file to be written relative to the process working directory, silently.

This is a Windows-only code path but can silently corrupt CI test state.

Key file: `libs/draxul-config/src/app_config_io.cpp`

## Investigation steps

- [x] Read `app_config_io.cpp` and find the full config path construction logic.
- [x] Confirm whether `APPDATA` is used directly or combined via `std::filesystem::path`.
- [x] Check whether the same file has similar issues for macOS (`HOME`, `XDG_CONFIG_HOME`).

## Fix strategy

- [x] After the null-check, add an empty-check:
  ```cpp
  if (appdata == nullptr || appdata[0] == '\0') {
      DRAXUL_LOG_WARN(LogCategory::Config, "APPDATA is not set; using fallback config path");
      // use fallback
  }
  ```
- [x] Apply the same pattern to any other environment variable reads in the same file (`HOME`, `XDG_CONFIG_HOME`).
- [x] Ensure the fallback path is a sensible absolute path (e.g. the user's home directory or the executable directory).

## Acceptance criteria

- [x] Setting `APPDATA=""` (or unsetting it) before launching does not produce a config file in the working directory.
- [x] A `WARN` log is emitted when the env var is empty.
- [x] `07 config-missing-malformed -test` exercises this code path.

## Interdependencies

- `07 config-missing-malformed -test`: test should include an empty-APPDATA scenario.
- `14 config-layer-decoupling -refactor`: touches same file; sequence this bug fix first.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
