# Make the startup window-error assertion effective

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

The startup rollback integration test appends `|| true` to its window-error assertion, making that assertion unconditional.

**Trigger:** Change `App::init_error()` so a window-creation failure returns unrelated nonempty text.

## Investigation

- [ ] Confirm the stable user-facing or semantic error contract for window creation failure.
- [ ] Search the rollback suite for other unconditional or tautological assertions.
- [ ] Verify the injected null window factory exercises the intended initialization stage.

## Fix strategy

- [ ] Remove `|| true`.
- [ ] Assert the stable expected window-failure text or error category.
- [ ] Keep the existing failure-return and clean-destruction assertions.

## Acceptance criteria

- [ ] The test fails when the window-specific error is absent.
- [ ] The test passes for the current documented window-creation failure.
- [ ] Startup rollback and app test suites pass.
