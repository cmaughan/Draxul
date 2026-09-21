# Recover from live-project parsing and filesystem exceptions

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1; GPT-6 Astra

`plugins/rezonality/src/live_project.cpp:413` and `:568` accept malformed numeric tokens before calling `std::stof`; `:1237` invokes the build without an exception barrier. Autosaving `field_of_view: -` or `scale: (1, -` terminates the worker’s process. Fingerprinting also contains throwing filesystem operations.

**Investigation**

- [ ] Exercise incomplete numbers, out-of-range values, and filesystem failures during watching.

**Fix strategy**

- [x] Use checked numeric parsing and error-code filesystem operations.
- [x] Convert build/watch exceptions into failed diagnostics while continuing the worker.

**Acceptance criteria**

- [x] Invalid edits preserve the last valid scene.
- [ ] Repairing the file successfully activates a later generation on both backends.
