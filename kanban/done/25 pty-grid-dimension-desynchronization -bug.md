# Keep PTY and terminal-grid dimensions synchronized

**Severity:** HIGH  
**Type:** Bug

## Bug description

Windows and Unix PTY backends clamp dimensions to 320×200 while the terminal grid accepts the full viewport, causing shell wrapping and cursor state to disagree with rendering.

**Trigger:** Use a large display or small font that produces more than 320 columns or 200 rows.

## Investigation

- [x] Identify all grid and PTY dimension limits and their platform type constraints.
- [x] Confirm the shared portable 16-bit terminal-dimension ceiling.
- [x] Add macOS validation above both legacy clamp thresholds; Windows remains pending.

## Fix strategy

- [x] Define one shared validated terminal-dimension policy.
- [x] Apply identical normalized dimensions to `TerminalCore`, Grid, ConPTY, and Unix PTY.
- [x] Preserve both backend implementations and surface resize failure rather than silently diverging.

## Acceptance criteria

- [x] The child-reported terminal size always matches the rendered grid size through the shared normalization boundary.
- [x] Widths above 320 and heights above 200 work up to the portable 32767-cell dimension limit.
- [x] macOS Unix PTY resize validation passes above both legacy clamp thresholds.
- [x] Windows ConPTY resize validation passes.

## Windows closeout (2026-09-22)

ConPTY dimension and resize coverage passed in the final 48/48 products
aggregate; same-cache smoke passed.
