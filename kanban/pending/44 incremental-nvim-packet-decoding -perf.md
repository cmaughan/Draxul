# Stop rebuilding fragmented Neovim packet prefixes

**Source:** `libs/draxul-nvim/src/rpc.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Codex; Claude listed the frequency as unresolved. Lines 467–478 retry decoding from the same prefix after an incomplete read; `mpack_codec.cpp:146–171` allocates a declared string or binary body before discovering it is incomplete. An 8 MiB payload over a 256 KiB read buffer is a concrete bounded workload.

- [ ] **Baseline:** Feed identical large packets in 4, 64, and 256 KiB fragments; count initialized bytes, decoded nodes, allocations, and decode time.
- [ ] **Implement:** Check body availability before allocation and retain incremental container progress or an equivalent linear decode plan.
- [ ] **Functional safety:** Preserve incomplete-versus-invalid handling, depth/size caps, response ordering, and EOF behavior.
- [ ] **Compare:** Require work proportional to payload rather than repeated prefix length.
- [ ] **Platforms:** Check native pipe transports on Windows and macOS, RPC aggregate and smoke.
- [ ] **Acceptance:** Fragmentation no longer repeatedly initializes or decodes completed prefixes.
