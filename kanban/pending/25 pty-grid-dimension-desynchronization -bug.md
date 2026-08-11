# Keep PTY and terminal-grid dimensions synchronized

**Severity:** HIGH  
**Type:** Bug

## Bug description

Windows and Unix PTY backends clamp dimensions to 320×200 while the terminal grid accepts the full viewport, causing shell wrapping and cursor state to disagree with rendering.

**Trigger:** Use a large display or small font that produces more than 320 columns or 200 rows.

## Investigation

- [ ] Identify all grid and PTY dimension limits and their platform type constraints.
- [ ] Confirm realistic maximum viewport sizes on Windows and macOS.
- [ ] Add tests that resize beyond both current clamp thresholds.

## Fix strategy

- [ ] Define one shared validated terminal-dimension policy.
- [ ] Apply identical normalized dimensions to `TerminalCore`, Grid, ConPTY, and Unix PTY.
- [ ] Preserve both backend implementations and surface resize failure rather than silently diverging.

## Acceptance criteria

- [ ] The child-reported terminal size always matches the rendered grid size.
- [ ] Widths above 320 and heights above 200 work up to the documented supported limit.
- [ ] Windows ConPTY and macOS Unix PTY resize validation passes.
