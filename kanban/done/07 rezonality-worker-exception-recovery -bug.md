# Recover from live-project parsing and filesystem exceptions

**Severity:** CRITICAL
**Reported by:** Claude Fable 5.1; GPT-6 Astra

`plugins/rezonality/src/live_project.cpp:413` and `:568` accept malformed numeric tokens before calling `std::stof`; `:1237` invokes the build without an exception barrier. Autosaving `field_of_view: -` or `scale: (1, -` terminates the worker’s process. Fingerprinting also contains throwing filesystem operations.

**Investigation**

- [x] Exercise incomplete numbers, out-of-range values, and filesystem failures during watching.

**Fix strategy**

- [x] Use checked numeric parsing and error-code filesystem operations.
- [x] Convert build/watch exceptions into failed diagnostics while continuing the worker.

**Acceptance criteria**

- [x] Invalid edits preserve the last valid scene.
- [x] Repairing the file successfully activates a later generation on both backends.

**Completion evidence**

- Numeric parsing now consumes complete scalar/vector tokens, rejects incomplete
  vectors and exponent overflow, and accepts a repaired scene in a later build.
- A production `LiveProject` watch test removes the project root, observes a
  controlled failed generation, restores the unchanged root, and observes a
  newer successful generation. Recovery scheduling is backend-neutral and feeds
  the existing Metal and Vulkan activation paths.
- Focused project coverage passed 78 assertions across six cases. The core plus
  Rezonality aggregate passed all 29 CTest entries in 106.80 seconds, including
  real Metal module edit/recovery coverage; same-cache Metal smoke passed on
  Apple M5. Vulkan was preserved by shared-source review but was not run locally.
