# SF Dash

`sfdash.ps1` is an adaptive terminal dashboard used to demonstrate dense,
animated Draxul pane layouts. Every view derives its canvas from the current
terminal width and height, so it continues to fill its pane after splits and
window resizes.

Run one view with PowerShell 7:

```powershell
pwsh -NoLogo -NoProfile -File demos/sfdash/sfdash.ps1 -Mode radar
```

Available modes are `map`, `dossier`, `drop`, `radar`, `cipher`, `bio`,
`spectrum`, `threat`, `network`, and `clock`. The default refresh interval is
1000 ms; use `-RefreshMilliseconds` to adjust it.

For smoke tests or screenshots, `-Frames 1` renders once and exits.

Press Ctrl+C to stop a view and restore the terminal cursor.
