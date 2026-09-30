# Type shared server vocabulary and terminal requests

**Priority:** P2 — repeated literals and request assembly increase wire drift risk.  
**Source:** `libs/draxul-protocol/include/draxul/remote_terminal_protocol.h`  
**Proposed by:** Claude 7, narrowed. **Owner:** one protocol agent. **Depends on:** card 02 for narrow tests.  
**Evidence:** client/server capability lists and terminal request construction are repeated; binary input codec already exists.

**Boundary verification**
- [ ] Separate supported, requested and required capability meanings and inventory exact wire names.
**Implementation and migration**
- [ ] Add named capability/method values and typed request codecs in existing protocol target; migrate callers by family and reuse binary-input codec.
**Unit tests**
- [ ] Round-trip valid/invalid requests, binary input and negotiation; test server support covers required client capabilities.
**Cross-platform validation**
- [ ] Run protocol aggregate on Windows/macOS and same-cache smoke.
**Agent documentation and tooling**
- [ ] Document current-format wire ownership without compatibility aliases.
**Acceptance criteria**
- [ ] No duplicate request construction remains for migrated methods; wire format is unchanged.
