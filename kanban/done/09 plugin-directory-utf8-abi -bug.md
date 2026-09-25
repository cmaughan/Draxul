# Publish plugin resource directories as explicit UTF-8
**Severity:** HIGH  
**Source:** Codex #10; `libs/draxul-host/src/plugin_host.cpp:117`.

The host publishes `.string()` bytes through `plugin_directory_utf8`; plugins decode them as UTF-8, breaking non-ASCII Windows staging paths.

**Investigation**

- [x] Trace the ABI directory field and representative plugin resource consumers.

**Fix strategy**

- [x] Populate the field using explicit UTF-8 conversion with lifetime extending through instance creation.

**Acceptance criteria**

- [x] The ABI fixture loads an asset from a non-ASCII Windows staging directory.
- [x] Confirm the behavior under a known non-UTF-8 Windows ANSI code page.
- [x] Run shared plugin aggregate coverage and same-cache startup.

**Implementation and evidence (2026-09-25)**

- `PluginHost` now copies `std::filesystem::path::u8string()` into its owned
  `plugin_directory_` before invoking `create_instance`; the SDK documents the
  UTF-8/lifetime contract. Representative consumers in Spinning Triangle,
  MegaCity, ScoreView, PCBView, SatView, and Rezonality already use `u8path`.
- The integration test also exposed a discovery prerequisite: toml++ was
  opening `plugin.toml` through `path.string()`, which passes ANSI bytes on
  Windows. Discovery now opens the native `std::filesystem::path` with
  `std::ifstream` and parses the UTF-8 content, leaving the package path wide.
- The ABI fixture integration test stages a plugin and asset under a directory
  containing both `é` and `猫`, then decodes the supplied directory as UTF-8
  and verifies the staged resource exists. This avoids relying on the machine's
  current ANSI code page to exercise the conversion boundary.
- Windows focused Unicode fixture: 1 case, 6 assertions passed. The full
  `py do.py test debug --products` aggregate passed 49/49 CTest entries in
  238.19 seconds. The same-cache startup passed with a 90-second bound in
  about 45 seconds; the standard 30-second smoke wrapper timed out twice on
  the existing nine-pane Session.
- Windows `GetACP()` returned 1252 (non-UTF-8) on 2026-09-25. Running the
  existing `PluginHost publishes non-ASCII resource directories as UTF-8`
  integration case directly from `draxul-test-app` passed 1 case and 6
  assertions. Its `café-猫` resource path includes a character not
  representable in code page 1252, so this directly checks the ACP boundary.
- macOS validation had not run at this stage; the platform-scope decision below
  supersedes it as a completion gate.

**Platform-scope decision (2026-09-25):** The failure required Windows' ANSI
path narrowing; the fix explicitly publishes UTF-8 from a native path. The
ACP-1252 integration uses a character not representable in that code page and
loads a real fixture resource. No changed macOS-specific path code remains to
gate this Windows path bug.
