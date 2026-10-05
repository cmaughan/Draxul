# Report unavailable temporary storage during printing

**Summary:** Show a printing error when temporary storage is unavailable so a failed print cannot close the application.

**Priority:** 71  
**Severity:** CRITICAL  
**Source:** `app/app.cpp`

**Evidence and trigger:** B09; line 2064 performs throwing temporary-directory lookup outside error handling after successful capture.

- [x] **Investigate:** Trace temporary-path creation through capture completion and print notifications.
  `App::end_frame` hands the captured frame to `App::finish_print_capture`, which called the
  throwing `std::filesystem::temp_directory_path()` with no handler; a missing or non-directory
  `TMPDIR` (`TMP`/`TEMP` on Windows) threw out of the frame loop. The same function also logged
  and toasted the PDF path via `path::string()`, which throws on Windows for temp paths outside
  the ANSI code page (for example a non-English user profile).
- [x] **Fix:** Use error-code lookup and report failure before constructing the output document.
  `pane_print_temp_pdf_path()` (`libs/draxul-runtime-support`) uses the `error_code` overload and
  returns a user-facing error; `finish_print_capture` resolves it first and toasts
  `Print failed: temporary storage is unavailable (...)` before cropping or composing the PDF.
  Path text for the log, toast, CoreGraphics URL and print dialog now goes through
  `pane_print_path_text()` (UTF-8 via `u8string`).
- [x] **Acceptance:** Missing or inaccessible temporary storage leaves the client running and produces a useful print error.
  `tests/pane_print_tests.cpp` points the OS temp lookup at a missing directory and at a regular
  file and asserts no throw plus an error; an unwritable directory still fails gracefully inside
  `write_rgba_pdf_a4` ("could not create PDF at ...").
- [x] **Validation:** Verify normal printing remains functional; run core aggregate tests and same-cache smoke.
  `[pane-print]` (6 cases, including the existing A4 PDF composition test) passes; the usable-temp
  case still yields a fresh `draxul-pane-<stamp>.pdf` path. Core aggregate and same-cache smoke
  pass on macOS. Windows printing remains unsupported (`write_rgba_pdf_a4` returns an error), so on
  Windows the change only removes the throwing lookup; the new tests run there in CI via
  `TMP`/`TEMP`.
