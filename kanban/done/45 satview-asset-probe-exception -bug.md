# Remove the throwing redundant SatView asset probe

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/core/satview_catalog.cpp:691` calls throwing `exists()` but returns the same path either way. An inaccessible asset ancestor can therefore terminate initialization or a refresh worker before ordinary file-read error handling runs.

**Investigation**

- [x] Trace bundled-catalog loading during initialization and refresh.

**Fix strategy**

- [x] Remove the redundant probe and rely on the existing read-error result.
- [x] Verify asset-access errors remain contained at worker boundaries.

**Acceptance criteria**

- [x] Missing or inaccessible bundled assets produce a controlled diagnostic.
- [x] Refresh failure preserves usable catalog state and does not terminate Draxul.
