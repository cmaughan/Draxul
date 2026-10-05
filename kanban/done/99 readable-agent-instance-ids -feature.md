# Give agents short, readable instance IDs

**Priority:** P2
**Summary:** Use names such as `happy-otter` directly in agent CLI commands, making agents easier to address by hand.

- [x] Generate collision-free word-pair instance IDs for managed, discovered, and manually attached agents.
- [x] Preserve managed identities on restart/restore and reserve existing identities across all Spaces within a Session.
- [x] Validate naming, exhaustion, discovery stability and actual managed-agent control; document usage.

Use one shared allocator per authority so managed and discovered names cannot collide. Never recycle issued names while that authority is running. After 4096 word pairs, add a numeric suffix. Names are Session-scoped; `--session` still selects the target Session. Existing stored identities remain opaque strings and are not rewritten.

## Completed 2026-10-05

- Added a shared `draxul-agent` allocator with 64 adjectives and 64 animals. A randomized starting point and permutation visit all 4096 pairs before adding suffixes. Every issued or reserved name remains unavailable for the allocator's lifetime.
- Managed and discovered server agents use the same Session allocator. Restored pane identities are reserved before the first discovery pass; runtime updates reserve all declared identities before processing discoveries. Client-local managed launch, discovery, and manual attachment use the same controller allocator, reserving identities across Spaces.
- Removed obsolete serial allocation and numeric-suffix recovery. The protocol already treats identities as strings, so command dispatch, integration environment variables, persistence, restart and restore keep their existing behavior. No stored IDs are renamed.
- Tests cover 8192 allocations with reserved plain/suffixed names, managed start/replay/control/move/restart/checkpoint/restore using short names, and discovery stability. Existing client controller cases verify readable names for discovery and manual attachment.
- First Release core aggregate: 55/56 passed in 24.93 seconds; exposed the existing dispatch-test lifetime/timing defects tracked and fixed in `kanban/done/100 dispatch-test-host-lifetime -test.md`. A focused diagnostic first removed the crash but retained the rejection timing failure; after fixing timing, all five generated scenarios passed five repeated runs (4.33 seconds).
- Final `python3 do.py test release`: all 56 CTest entries passed in 19.65 seconds (25.26 seconds including incremental build/wrapper). `python3 do.py smoke release --skip-build`: passed in 1.09 seconds, same cache. Initial configure/compile were not timed separately. No renderer or layout changes; no additional render snapshot required. Windows behavior uses the same implementation and existing automated coverage.
- Built the current Release app/helper but did not stop the user's running server. Restart that server to allocate short names for new agents; existing managed IDs stay unchanged.
