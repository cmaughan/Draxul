# Make the startup window-error assertion effective

**Severity:** MEDIUM
**Type:** Bug

## Bug description

The startup rollback integration test appends `|| true` to its window-error assertion, making that assertion unconditional.

**Trigger:** Change `App::init_error()` so a window-creation failure returns unrelated nonempty text.

## Investigation

- [x] Confirm the stable user-facing or semantic error contract for window creation failure.
- [x] Search the rollback suite for other unconditional or tautological assertions.
- [x] Verify the injected null window factory exercises the intended initialization stage.

## Fix strategy

- [x] Remove `|| true`.
- [x] Assert the stable expected window-failure text or error category.
- [x] Keep the existing failure-return and clean-destruction assertions.

## Acceptance criteria

- [x] The test fails when the window-specific error is absent.
- [x] The test passes for the current documented window-creation failure.
- [x] Startup rollback and app test suites pass.
