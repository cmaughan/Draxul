# Preserve build lifetime while publishing activation status

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/rezonality_plugin.cpp:3191` and `:3494` reset `pending_build`, then read its destroyed object through `desired`. A successful pending shader activation reaches this lifetime violation on Metal and Vulkan.

**Investigation**

- [ ] Trace `desired` ownership for pending activation and resize recreation.

**Fix strategy**

- [ ] Build status from the committed `active_build`, or capture required values before resetting the pending build.
- [ ] Audit both success and failure branches for invalidated aliases.

**Acceptance criteria**

- [ ] Repeated successful reloads produce correct counts without lifetime violations.
- [ ] Failed reloads and resize recreation retain valid active state.
