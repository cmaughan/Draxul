# Full Neovim `guicursor` support

**Type:** feature
**Disposition:** Implemented

Neovim UI decoding parses cursor shape, cell percentage, highlight attributes, and
blink timing; `NvimHost` applies mode-specific state; renderer state draws block,
vertical, and horizontal cursors. UI-event, cursor-blinker, and renderer-state tests
cover the resulting behavior.
