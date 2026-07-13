# `do.py clean` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an idempotent, cross-platform `do.py clean` command that removes only the repository-local `build/` directory.

**Architecture:** Keep path selection inside `do.py`: `cmd_clean()` derives the target through `build_dir(repo_root())`, then delegates recursive deletion to one shared `_remove_tree()` helper. Extract the deployment staging code's existing read-only retry behavior into that helper so clean and deploy use the same Windows-safe deletion path.

**Tech Stack:** Python 3 standard library (`pathlib`, `shutil`, `os`, `stat`, `unittest`), Draxul's existing `do.py` command dispatcher, Markdown documentation.

---

### Task 1: Specify the clean command with failing tests

**Files:**
- Modify: `tests/do_py_tests.py:1-150`
- Test: `tests/do_py_tests.py`

- [ ] **Step 1: Add output-capture imports and clean-command tests**

Add these imports near the top of `tests/do_py_tests.py`:

```python
import contextlib
import io
```

Add this class after `BuildCacheTests` and before `TestCommandTests`:

```python
class CleanCommandTests(unittest.TestCase):
    def test_help_lists_clean_command(self) -> None:
        self.assertIn("clean        Remove the repository build directory", draxul_do.help_text())

    def test_clean_removes_build_and_preserves_neighboring_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            build_file = root / "build" / "CMakeFiles" / "cache.txt"
            build_file.parent.mkdir(parents=True)
            build_file.write_text("generated")
            deploy_file = root / "deploy" / "package.zip"
            deploy_file.parent.mkdir()
            deploy_file.write_text("keep")
            output = io.StringIO()

            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "clean"]),
            ):
                self.assertEqual(0, draxul_do.main())

            self.assertFalse((root / "build").exists())
            self.assertEqual("keep", deploy_file.read_text())
            self.assertIn("Removing build directory", output.getvalue())

    def test_clean_succeeds_when_build_is_already_absent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            output = io.StringIO()

            with (
                contextlib.redirect_stdout(output),
                mock.patch.object(draxul_do, "repo_root", return_value=root),
                mock.patch.object(draxul_do.sys, "argv", ["do.py", "clean"]),
            ):
                self.assertEqual(0, draxul_do.main())

            self.assertIn("Build directory already absent", output.getvalue())
```

- [ ] **Step 2: Run the focused tests and verify the command is not implemented**

Run:

```bash
python3 -m unittest tests.do_py_tests.CleanCommandTests -v
```

Expected: failures because help does not list `clean` and `main()` treats it as an unknown command, leaving the temporary `build/` directory in place.

### Task 2: Implement bounded cross-platform deletion

**Files:**
- Modify: `do.py:31-40`
- Modify: `do.py:567-579`
- Modify: `do.py:1261-1340`
- Test: `tests/do_py_tests.py`

- [ ] **Step 1: Extract the shared recursive-removal helper**

Add this helper immediately after `build_dir()`:

```python
def _remove_tree(path: pathlib.Path) -> None:
    def remove_readonly(function, failed_path, _error_info):
        os.chmod(failed_path, stat.S_IWRITE)
        function(failed_path)

    shutil.rmtree(path, onerror=remove_readonly)
```

Replace the inline deletion block in `_stage_deploy_payload()` with:

```python
    if platform_dir.exists():
        _remove_tree(platform_dir)
```

- [ ] **Step 2: Add the command function**

Add this function immediately before `help_text()`:

```python
def cmd_clean(root: pathlib.Path) -> int:
    bd = build_dir(root)
    if not bd.exists():
        print(f"Build directory already absent: {bd}")
        return 0

    print(f"Removing build directory: {bd}")
    _remove_tree(bd)
    return 0
```

- [ ] **Step 3: Register and advertise the command**

Add the command to the single-word shortcut list in `help_text()`:

```text
  clean        Remove the repository build directory
```

Add `do clean` to the help examples, then register it in `main()` before the `test` branch:

```python
    if command == "clean":
        return cmd_clean(root)
```

- [ ] **Step 4: Run the focused tests and verify they pass**

Run:

```bash
python3 -m unittest tests.do_py_tests.CleanCommandTests -v
```

Expected: all three clean-command tests pass.

- [ ] **Step 5: Run the complete `do.py` unit suite**

Run:

```bash
python3 -m unittest tests.do_py_tests -v
```

Expected: `OK`; the Windows executable-layout test may be skipped on non-Windows platforms.

- [ ] **Step 6: Commit the implementation and tests**

```bash
git add do.py tests/do_py_tests.py
git commit -m "feat: add do.py clean command"
```

### Task 3: Document the destructive boundary

**Files:**
- Modify: `README.md:189-235`
- Modify: `docs/features.md:388-400`

- [ ] **Step 1: Add the command to the README examples**

Add this line next to the other build and test shortcuts:

```text
./do.py clean        # remove only the repository build/ directory
```

Add the Windows spelling to the Windows example block:

```text
do clean                 # Remove only build/
```

After the command block, state:

```markdown
`do.py clean` removes only the repository-local `build/` directory. It leaves deploy packages, render outputs, render references, databases, and source files untouched; running it when `build/` is already absent succeeds.
```

- [ ] **Step 2: Add the feature documentation entry**

Add this bullet under `docs/features.md`'s Convenience Scripts section:

```markdown
- `do clean` recursively removes only the repository-local `build/` directory and succeeds when it is already absent. Deploy packages, render outputs and references, databases, and source files are preserved
```

- [ ] **Step 3: Verify documentation and help agree**

Run:

```bash
python3 do.py --help | rg "clean|repository build directory"
rg -n "do(\.py)? clean|do clean" README.md docs/features.md
git diff --check
```

Expected: help, README, and feature documentation all describe the `build/`-only boundary; `git diff --check` reports no errors.

- [ ] **Step 4: Commit the documentation**

```bash
git add README.md docs/features.md
git commit -m "docs: document do.py clean"
```

### Task 4: Verify the command against the real checkout and rebuild

**Files:**
- Verify: `do.py`
- Verify: `tests/do_py_tests.py`
- Verify: `README.md`
- Verify: `docs/features.md`

- [ ] **Step 1: Run the Python command tests once more before deleting build outputs**

Run:

```bash
python3 -m unittest tests.do_py_tests -v
```

Expected: `OK` with no failures.

- [ ] **Step 2: Remove the checkout's build directory through the new command**

Run:

```bash
python3 do.py clean
test ! -e build
```

Expected: the command reports the absolute `build/` path, exits 0, and `test` confirms the directory is absent.

- [ ] **Step 3: Verify idempotence on the real checkout**

Run:

```bash
python3 do.py clean
```

Expected: exit 0 with `Build directory already absent`.

- [ ] **Step 4: Reconfigure, rebuild, and run the unit-only workflow from the clean state**

Run:

```bash
python3 do.py test
```

Expected: configuration succeeds, `draxul-tests` builds, and all four C++ shards plus `draxul-do-py-tests` pass.

- [ ] **Step 5: Build the application and unit targets, then run the required smoke test**

Run:

```bash
cmake --build build --target draxul draxul-tests --parallel 8
python3 do.py smoke
```

Expected: both targets build and the application smoke test exits 0.

- [ ] **Step 6: Confirm the final branch is clean and review its commits**

Run:

```bash
git diff --check
git status --short
git log --oneline main..HEAD
```

Expected: no whitespace errors, no uncommitted files, and the design, implementation, and documentation commits are listed.
