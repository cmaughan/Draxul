# Preserve build lifetime while publishing activation status

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/rezonality_plugin.cpp:3191` and `:3494` reset `pending_build`, then read its destroyed object through `desired`. A successful pending shader activation reaches this lifetime violation on Metal and Vulkan.

**Investigation**

- [x] Trace `desired` ownership for pending activation and resize recreation.

**Fix strategy**

- [x] Build status from captured pending-build values before resetting the pending build.
- [x] Audit both success and failure branches for invalidated aliases.

**Acceptance criteria**

- [x] Repeated successful reloads produce correct counts without lifetime violations.
- [x] Failed reloads and resize recreation retain valid active state.
