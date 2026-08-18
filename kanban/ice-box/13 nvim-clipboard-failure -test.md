# Neovim clipboard-provider failure coverage

**Type:** test
**Priority:** 13

Clipboard set is an incoming notification and clipboard get is an incoming request served
from a protected cache, so the old outgoing-RPC timeout scenarios were impossible.

- [ ] Cover malformed `nvim_get_api_info` and provider-install request failure.
- [ ] Inject SDL clipboard write failure and verify the stable warning/toast result.
- [ ] Race cache refresh with an incoming clipboard-get request and shutdown.
- [ ] Verify malformed provider requests fail cleanly without corrupting the last valid cache.
- [ ] Run focused Nvim/clipboard tests and same-cache smoke.
