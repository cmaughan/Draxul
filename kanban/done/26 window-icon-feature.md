---
# Window Icon (ICNS / RC)

## Summary
Draxul window shows the default SDL blank window icon. A 256×256 application icon embedded via CMake's ICNS (macOS) or RC (Windows) significantly improves application identity for no ongoing maintenance cost.

## Context
`CMakeLists.txt` at root. `app/main.cpp`. Look for `WIN32_EXECUTABLE` and icon references. Both platforms have well-established CMake patterns for embedding icons; this is a one-time asset + wiring change.

## Steps
- [x] Create `assets/` directory and place a `draxul.png` (256×256) icon there (placeholder acceptable)
- [x] macOS: convert PNG to `draxul.icns` using `iconutil`, reference in `CMakeLists.txt` for the app target
- [x] Windows: create `app/draxul.rc` with `IDI_ICON1 ICON "draxul.ico"`, add to app sources
- [x] Verify icon appears in macOS Dock and Windows taskbar
- [x] The icon asset file should be committed to `assets/`

## Acceptance Criteria
- [x] App icon visible in Dock/taskbar on both platforms
- [x] No change to app behaviour

## Notes
macOS: `assets/draxul.iconset/` (9 sizes from 16x16 to 512x512) converted to `assets/draxul.icns`
via `iconutil`. App target set to `MACOSX_BUNDLE TRUE` with `MACOSX_BUNDLE_ICON_FILE "draxul"`.
ICNS copied to `Contents/Resources/draxul.icns` at build time. `CFBundleIconFile` in Info.plist
is wired automatically by CMake. Bundle binary is at `draxul.app/Contents/MacOS/draxul`.

Windows: `app/draxul.ico` (6 sizes: 16, 32, 48, 64, 128, 256 px) generated from the PNG using
Pillow. `app/draxul.rc` references `IDI_ICON1 ICON "draxul.ico"`. The RC file is added to the
`draxul` executable sources when `WIN32` is set, which makes MSVC link the icon resource.

*Work item implemented by claude-sonnet-4-6*
