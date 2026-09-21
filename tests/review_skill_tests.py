from __future__ import annotations

import importlib.util
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER = ROOT / ".agents" / "skills" / "draxul-review" / "scripts" / "review.py"
SPEC = importlib.util.spec_from_file_location("draxul_review_runner", RUNNER)
assert SPEC and SPEC.loader
review = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = review
SPEC.loader.exec_module(review)


class ReviewerSelectionTests(unittest.TestCase):
    def test_default_panel_uses_astra_and_fable(self) -> None:
        panel = review.requested_panel([], False)
        self.assertEqual([("codex", ""), ("claude", "")], panel)
        self.assertEqual(
            ["gpt-6-astra", "claude-fable-5-1"],
            [review.ADAPTERS[transport].default_model for transport, _ in panel],
        )

    def test_all_panel_adds_google_and_xai(self) -> None:
        panel = review.requested_panel([], True)
        self.assertEqual(4, len(panel))
        self.assertIn(("google", ""), panel)
        self.assertIn(("grok", ""), panel)

    def test_default_preflight_checks_only_the_review_panel(self) -> None:
        args = review.build_parser().parse_args(["preflight"])

        def probe(request, timeout):
            transport, model = request
            adapter = review.ADAPTERS[transport]
            result = review.ProbeResult(
                requested=transport, ok=True, message="ready",
                reviewer=review.Reviewer(
                    transport, adapter.company, model or adapter.default_model, transport
                ),
            )
            return result, [result]

        with mock.patch.object(review, "resolve_and_probe", side_effect=probe) as resolve:
            self.assertEqual(0, review.command_preflight(args))
        self.assertEqual(
            [("codex", ""), ("claude", "")],
            [call.args[0] for call in resolve.call_args_list],
        )

    def test_models_are_passed_through(self) -> None:
        self.assertEqual(("codex", "future-model"), review.parse_reviewer("codex:future-model"))
        reviewer = review.Reviewer("codex", "openai", "future-model", "codex")
        with mock.patch.object(review, "executable_prefix", return_value=["codex"]):
            command, _ = review.agent_command(
                reviewer, pathlib.Path("D:/snapshot"), "prompt", pathlib.Path("D:/out.md"), 42
            )
        self.assertIn("future-model", command)
        self.assertIn("--json", command)
        self.assertIn("read-only", command)
        self.assertNotIn("danger-full-access", command)

    def test_codex_snapshot_fallback_can_disable_broken_windows_sandbox(self) -> None:
        reviewer = review.Reviewer("codex", "openai", "gpt-5.6-sol", "codex")
        with mock.patch.object(review, "executable_prefix", return_value=["codex"]):
            command, _ = review.agent_command(
                reviewer,
                pathlib.Path("D:/snapshot"),
                "prompt",
                pathlib.Path("D:/out.md"),
                42,
                sandbox_mode="danger-full-access",
            )
        self.assertNotIn('windows.sandbox="unelevated"', command)
        self.assertIn("danger-full-access", command)

    def test_every_provider_command_uses_its_read_only_contract(self) -> None:
        expected = {
            "codex": {"--sandbox", "read-only", "--skip-git-repo-check", "mcp_servers={}"},
            "claude": {"--permission-mode", "plan", "--allowedTools", "Read,Grep,Glob", "--strict-mcp-config"},
            "agy": {"--sandbox"},
            "gemini": {"--approval-mode", "plan"},
            "grok": {"--permission-mode", "plan", "--no-subagents", "--disable-web-search", "--tools"},
        }
        forbidden = {"danger-full-access", "--dangerously-skip-permissions", "--yolo", "--always-approve"}
        with mock.patch.object(review, "executable_prefix", side_effect=lambda command: [command]):
            for transport, required in expected.items():
                adapter = review.ADAPTERS[transport]
                reviewer = review.Reviewer(transport, adapter.company, "future-model", transport)
                command, _ = review.agent_command(
                    reviewer, pathlib.Path("D:/snapshot"), "prompt", pathlib.Path("D:/out.md"), 42
                )
                self.assertTrue(required.issubset(set(command)), (transport, command))
                self.assertTrue(forbidden.isdisjoint(command), (transport, command))
                self.assertIn("future-model", command)
                self.assertNotIn("--ephemeral", command)
                self.assertNotIn("--no-session-persistence", command)
                if transport == "codex":
                    self.assertIn('model_reasoning_effort="high"', command)
                if transport == "claude":
                    self.assertEqual("high", command[command.index("--effort") + 1])

    def test_preflight_commands_disable_session_persistence(self) -> None:
        with mock.patch.object(review, "executable_prefix", side_effect=lambda command: [command]):
            codex = review.Reviewer("codex", "openai", "gpt-5.6-sol", "codex")
            codex_command, _ = review.agent_command(
                codex,
                pathlib.Path("D:/preflight"),
                "prompt",
                pathlib.Path("D:/out.md"),
                42,
                persist_session=False,
            )
            claude = review.Reviewer("claude", "anthropic", "opus", "claude")
            claude_command, _ = review.agent_command(
                claude,
                pathlib.Path("D:/preflight"),
                "prompt",
                pathlib.Path("D:/out.md"),
                42,
                persist_session=False,
            )
        self.assertIn("--ephemeral", codex_command)
        self.assertIn("--no-session-persistence", claude_command)

    def test_duplicate_company_is_rejected(self) -> None:
        with self.assertRaisesRegex(review.ReviewError, "both belong to google"):
            review.requested_panel(["agy", "gemini"], False)

    def test_panel_is_limited_to_four_reviewers(self) -> None:
        with self.assertRaisesRegex(review.ReviewError, "between one and four"):
            review.requested_panel(["codex", "claude", "agy", "grok", "codex"], False)

    def test_kanban_defaults_on_and_can_be_disabled(self) -> None:
        parser = review.build_parser()
        for command in (["review", "--prompt-file", "prompt.md"],
                        ["summarize", "--prompt-file", "prompt.md", "--run", "run-id"]):
            self.assertTrue(parser.parse_args(command).kanban)
            self.assertFalse(parser.parse_args([*command, "--no-kanban"]).kanban)
        args = parser.parse_args(["review", "--prompt-file", "prompt.md", "--no-consensus"])
        self.assertTrue(args.no_consensus)

    def test_summarize_requires_one_input_mode(self) -> None:
        parser = review.build_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args(["summarize", "--prompt-file", "prompt.md"])
        with self.assertRaises(SystemExit):
            parser.parse_args(
                [
                    "summarize",
                    "--prompt-file",
                    "prompt.md",
                    "--run",
                    "one",
                    "--input",
                    "two.md",
                ]
            )

    def test_google_falls_back_to_gemini_and_records_reason(self) -> None:
        def fake_probe(transport: str, model: str, timeout: int) -> review.ProbeResult:
            del timeout
            adapter = review.ADAPTERS[transport]
            reviewer = review.Reviewer(
                transport, adapter.company, model or adapter.default_model, transport
            )
            if transport == "agy":
                return review.ProbeResult("agy", False, reviewer, "please sign in")
            return review.ProbeResult("gemini", True, reviewer, "live response verified")

        selected, attempts = review.resolve_and_probe(("google", "pro"), 10, fake_probe)
        self.assertTrue(selected.ok)
        self.assertEqual("gemini", selected.reviewer.transport)
        self.assertEqual("pro", selected.reviewer.model)
        self.assertIn("Agy unavailable", selected.reviewer.fallback_reason)
        self.assertEqual(["agy", "gemini"], [attempt.requested for attempt in attempts])


class SnapshotTests(unittest.TestCase):
    def init_repo(self, path: pathlib.Path) -> None:
        subprocess.run(["git", "init", "-q", str(path)], check=True)
        subprocess.run(["git", "-C", str(path), "config", "user.email", "test@example.com"], check=True)
        subprocess.run(["git", "-C", str(path), "config", "user.name", "Test"], check=True)
        (path / "source.txt").write_text("original\n", encoding="utf-8")
        (path / ".gitignore").write_text("ignored.txt\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(path), "add", "source.txt", ".gitignore"], check=True)
        subprocess.run(["git", "-C", str(path), "commit", "-qm", "initial"], check=True)

    def test_snapshot_includes_untracked_but_excludes_reviews_and_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            (root / "untracked.md").write_text("new\n", encoding="utf-8")
            (root / "ignored.txt").write_text("ignored\n", encoding="utf-8")
            (root / "plans" / "reviews").mkdir(parents=True)
            (root / "plans" / "reviews" / "old.md").write_text("old\n", encoding="utf-8")
            snapshot = pathlib.Path(temp) / "snapshot"
            review.copy_source_snapshot(root, snapshot)
            self.assertTrue((snapshot / "source.txt").exists())
            self.assertTrue((snapshot / "untracked.md").exists())
            self.assertFalse((snapshot / "ignored.txt").exists())
            self.assertFalse((snapshot / "plans" / "reviews" / "old.md").exists())
            self.assertTrue((snapshot / "REPO_STATE.md").exists())

    def test_snapshot_recurses_gitlinks_and_preserves_dirty_submodule_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            product = root / "plugins" / "product"
            product.mkdir(parents=True)
            self.init_repo(product)
            nested = product / "nested"
            nested.mkdir()
            self.init_repo(nested)
            subprocess.run(["git", "-C", str(product), "add", "nested"], check=True, capture_output=True)
            subprocess.run(["git", "-C", str(root), "add", "plugins/product"], check=True, capture_output=True)
            (nested / "source.txt").write_text("dirty nested source\n")
            (product / "new.cpp").write_text("untracked source\n")
            (product / "ignored.txt").write_text("ignored\n")
            (product / "plans" / "reviews").mkdir(parents=True)
            (product / "plans" / "reviews" / "old.md").write_text("old review")
            snapshot = pathlib.Path(temp) / "snapshot"
            files = review.copy_source_snapshot(root, snapshot)
            self.assertEqual("dirty nested source\n", (snapshot / "plugins/product/nested/source.txt").read_text())
            self.assertIn(pathlib.Path("plugins/product/new.cpp"), files)
            self.assertNotIn(pathlib.Path("plugins/product/ignored.txt"), files)
            self.assertNotIn(pathlib.Path("plugins/product/plans/reviews/old.md"), files)
            self.assertFalse((snapshot / "plugins/product/.git").exists())
            shutil.rmtree(nested / ".git")
            with self.assertRaisesRegex(review.ReviewError, "not initialized"):
                review.snapshot_file_list(root)

    def test_repomix_runs_once_then_each_reviewer_gets_one_private_file(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            (root / "prompt.md").write_text("Find bugs across component boundaries.")
            (root / "repomix-output.xml").write_text("stale generated output")
            product = root / "plugins" / "product"
            product.mkdir(parents=True)
            self.init_repo(product)
            subprocess.run(["git", "-C", str(root), "add", "plugins/product"], check=True, capture_output=True)
            (product / "source.txt").write_text("dirty product source")
            output = pathlib.Path(temp) / "output"
            packed = '<repomix><file path="source.txt">original</file></repomix>'
            calls = []
            workspaces = []

            def fake_run(command, cwd, **kwargs):
                calls.append(command[0])
                if command[0] == "repomix":
                    paths = kwargs["input_text"].splitlines()
                    self.assertIn("plugins/product/source.txt", paths)
                    self.assertNotIn("repomix-output.xml", paths)
                    self.assertNotIn("REPO_STATE.md", paths)
                    self.assertEqual("dirty product source", (cwd / "plugins/product/source.txt").read_text())
                    pathlib.Path(command[command.index("--output") + 1]).write_text(packed)
                    return subprocess.CompletedProcess(command, 0, "Packed source files", "")
                workspaces.append(cwd)
                self.assertEqual({"repomix-output.xml", "REVIEW_PROMPT.md"}, {p.name for p in cwd.iterdir()})
                self.assertEqual(packed, (cwd / "repomix-output.xml").read_text())
                self.assertIn("sole source input", kwargs["input_text"])
                self.assertNotIn("SOURCE SEGMENTS", kwargs["input_text"])
                report = "# Review\n\n- A cross-component finding."
                if "--output-last-message" in command:
                    pathlib.Path(command[command.index("--output-last-message") + 1]).write_text(report)
                return subprocess.CompletedProcess(command, 0, report, "")

            def probe(request, timeout):
                transport, model = request
                adapter = review.ADAPTERS[transport]
                result = review.ProbeResult(transport, True,
                    review.Reviewer(transport, adapter.company, model or adapter.default_model, transport))
                return result, [result]

            with (
                mock.patch.object(review, "executable_prefix", side_effect=lambda name: [name]),
                mock.patch.object(review, "run_process", side_effect=fake_run),
                mock.patch.object(review, "resolve_and_probe", side_effect=probe),
            ):
                self.assertEqual(0, review.main(["review", "--repo-root", str(root),
                    "--output-root", str(output), "--prompt-file", "prompt.md", "--no-consensus"]))
            self.assertEqual("repomix", calls[0])
            self.assertCountEqual(["repomix", "codex", "claude"], calls)
            self.assertEqual(2, len(set(workspaces)))
            run = next((output / "runs").iterdir())
            manifest = json.loads((run / "manifest.json").read_text())
            self.assertEqual("complete", manifest["status"])
            self.assertEqual(review.hashlib.sha256(packed.encode()).hexdigest(), manifest["input"]["sha256"])
            self.assertEqual(2, len(list((run / "reports").glob("*.md"))))
            self.assertFalse((run / "batches").exists())
            self.assertFalse((run / "coverage").exists())
            self.assertEqual("stale generated output", (root / "repomix-output.xml").read_text())

    def test_immediate_publication_then_consensus_and_optional_cards(self) -> None:
        for kanban, failed_review, failed_summary in (
            (False, False, False), (True, False, False),
            (False, True, False), (False, False, True),
        ):
            with self.subTest(kanban=kanban, failed_review=failed_review,
                              failed_summary=failed_summary), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp) / "repo"
                root.mkdir()
                self.init_repo(root)
                (root / "review_bugs.md").write_text("Find bugs.")
                prompts = root / "plans/prompts"
                prompts.mkdir(parents=True)
                (prompts / "consensus_review_bugs.md").write_text(
                    "Saved bug triage criteria. Create cards in kanban/pending/."
                )
                output = pathlib.Path(temp) / "output"
                durable = threading.Event()
                calls = []
                original_publish = review.publish_review_artifacts

                def publish(*args, **kwargs):
                    manifest = original_publish(*args, **kwargs)
                    if any(e["status"] == "passed" for e in manifest["reviewers"]):
                        durable.set()
                    return manifest

                def pack(snapshot, files, run, timeout):
                    target = run / review.REPOMIX_OUTPUT
                    target.write_text("<repomix>source</repomix>")
                    return target

                def probe(request, timeout):
                    transport, model = request
                    adapter = review.ADAPTERS[transport]
                    result = review.ProbeResult(transport, True,
                        review.Reviewer(transport, adapter.company, model or adapter.default_model, transport))
                    return result, [result]

                def provider(reviewer, repo, prompt, timeout, **kwargs):
                    if kwargs.get("summary"):
                        calls.append("consensus")
                        self.assertIn("Saved bug triage criteria", prompt)
                        self.assertIn("not latest-file globs", prompt)
                        inputs = kwargs["summary_inputs"]
                        self.assertEqual(1 if failed_review else 2, len(inputs))
                        self.assertEqual(1, len({p.parent.parent for p in inputs}))
                        review_run = inputs[0].parent.parent
                        manifest = json.loads((review_run / "manifest.json").read_text())
                        self.assertNotEqual("running", manifest["status"])
                        self.assertTrue(all(p.is_file() for p in inputs))
                        if failed_summary:
                            return review.AgentResult(reviewer, False, error="synthesis unavailable")
                        # Even an unwanted provider card must not be materialized by default.
                        return review.AgentResult(reviewer, True,
                            "# Consensus\n\nConfirmed bug.\n\n"
                            "### kanban/pending/00 confirmed -bug.md\n"
                            "# Confirmed bug\n\n- [ ] Fix and verify.\n")
                    calls.append(reviewer.transport)
                    if reviewer.transport == "claude":
                        self.assertTrue(durable.wait(5), "Astra report was not published while Fable was running")
                        run = next((output / "runs").iterdir())
                        self.assertTrue(list((run / "reports").glob("openai.*.md")))
                        manifest = json.loads((run / "manifest.json").read_text())
                        self.assertEqual("running", manifest["status"])
                        self.assertIsNone(manifest["finished_at"])
                        self.assertEqual("running", manifest["reviewers"][1]["status"])
                        if failed_review:
                            return review.AgentResult(reviewer, False, error="review unavailable")
                    return review.AgentResult(reviewer, True, "# Review\n\n- A real finding.", stderr="diagnostics")

                argv = ["review", "--repo-root", str(root), "--output-root", str(output),
                        "--prompt-file", "review_bugs.md"] + ([] if kanban else ["--no-kanban"])
                with (
                    mock.patch.object(review, "pack_repository", side_effect=pack),
                    mock.patch.object(review, "resolve_and_probe", side_effect=probe),
                    mock.patch.object(review, "execute_agent", side_effect=provider),
                    mock.patch.object(review, "publish_review_artifacts", side_effect=publish),
                ):
                    self.assertEqual(int(failed_review or failed_summary), review.main(argv))
                self.assertCountEqual(["codex", "claude", "consensus"], calls)
                manifest = json.loads((output / "review-bugs-latest.manifest.json").read_text())
                state = "failed" if failed_summary else "partial" if failed_review else "complete"
                self.assertEqual(state, manifest["consensus"]["status"])
                review_run = output / "runs" / manifest["run_id"]
                self.assertTrue(list((review_run / "reports").glob("openai.*.md")))
                self.assertTrue((review_run / "logs/codex.stderr.log").exists())
                self.assertEqual(kanban, (root / "kanban/pending/00 confirmed -bug.md").exists())
                if not failed_summary:
                    summary = json.loads((output / "review-bugs-latest.summary.manifest.json").read_text())
                    self.assertEqual(1 if kanban else 0, len(summary["work_items"]))
                    self.assertEqual(bool(failed_review), bool(summary["missing_reviewers"]))

    def test_repomix_failure_prevents_provider_calls(self) -> None:
        for failure in ("exit", "empty", "missing-cli"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp) / "repo"
                root.mkdir()
                self.init_repo(root)
                (root / "prompt.md").write_text("Find bugs.")
                output = pathlib.Path(temp) / "output"
                with (
                    mock.patch.object(review, "executable_prefix", return_value=["repomix"]) as prefix,
                    mock.patch.object(review, "run_process", return_value=
                        subprocess.CompletedProcess([], 1 if failure == "exit" else 0, "", "pack failed")),
                    mock.patch.object(review, "resolve_and_probe") as probe,
                ):
                    if failure == "missing-cli":
                        prefix.side_effect = review.ReviewError("repomix CLI is not on PATH.")
                    self.assertEqual(1, review.main(["review", "--repo-root", str(root),
                        "--output-root", str(output), "--prompt-file", "prompt.md"]))
                    probe.assert_not_called()
                run = next((output / "runs").iterdir())
                self.assertEqual("failed", json.loads((run / "manifest.json").read_text())["status"])

    def test_plan_only_packs_without_invoking_providers(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            (root / "prompt.md").write_text("Review all files.")
            output = pathlib.Path(temp) / "output"

            def pack(command, cwd, **kwargs):
                pathlib.Path(command[command.index("--output") + 1]).write_text("<repomix>source</repomix>")
                return subprocess.CompletedProcess(command, 0, "packed", "")

            with (
                mock.patch.object(review, "resolve_and_probe") as probe,
                mock.patch.object(review, "executable_prefix", return_value=["repomix"]),
                mock.patch.object(review, "run_process", side_effect=pack) as run_process,
            ):
                self.assertEqual(0, review.main(["review", "--repo-root", str(root),
                    "--output-root", str(output), "--prompt-file", "prompt.md", "--plan-only"]))
                probe.assert_not_called()
                run_process.assert_called_once()
            run = next((output / "runs").iterdir())
            self.assertEqual("planned", json.loads((run / "manifest.json").read_text())["status"])
            self.assertTrue((run / "repomix-output.xml").exists())

    def test_malicious_agent_only_changes_disposable_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            reviewer = review.Reviewer("codex", "openai", "fake", "codex")

            def fake_command(*args, **kwargs):
                del args, kwargs
                code = (
                    "from pathlib import Path; "
                    "Path('source.txt').write_text('mutated\\n', encoding='utf-8'); "
                    "print('# Review\\n\\nThe disposable snapshot was inspected safely.')"
                )
                return [sys.executable, "-c", code], None

            with mock.patch.object(review, "agent_command", side_effect=fake_command):
                result = review.execute_agent(reviewer, root, "review prompt", 20)

            self.assertTrue(result.ok, result.error)
            self.assertEqual("original\n", (root / "source.txt").read_text(encoding="utf-8"))
            self.assertIn("disposable snapshot", result.output)

    def test_codex_reviewed_error_strings_do_not_fail_or_trigger_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            reviewer = review.Reviewer("codex", "openai", "fake", "codex")
            quoted = "CreateProcessAsUserW failed: 1312 (A specified logon session does not exist.)"
            report = "# Review\n\n- Source contains the diagnostic: " + quoted
            events = [
                {"type": "item.completed", "item": {"type": "command_execution", "aggregated_output": quoted}},
                {"type": "item.completed", "item": {"type": "agent_message", "text": report}},
                {"type": "turn.completed", "usage": {}},
            ]

            def fake_run_process(command, cwd, **kwargs):
                pathlib.Path(command[command.index("--output-last-message") + 1]).write_text(report)
                return subprocess.CompletedProcess(command, 0, "\n".join(map(json.dumps, events)), "")

            windows_os = mock.Mock(wraps=review.os)
            windows_os.name = "nt"
            with (
                mock.patch.object(review, "executable_prefix", return_value=["codex"]),
                mock.patch.object(review, "run_process", side_effect=fake_run_process) as run,
                mock.patch.object(review, "os", windows_os),
            ):
                result = review.execute_agent(reviewer, root, quoted, 20)
            self.assertTrue(result.ok, result.error)
            self.assertEqual(1, run.call_count)
            self.assertFalse(result.sandbox_fallback)
            self.assertIn(quoted, result.output)

    def test_codex_structured_runtime_errors_reject_plausible_report(self) -> None:
        message = "windows sandbox: runner failed during SpawnChild: CreateProcessAsUserW failed: 1312"
        events = [
            {"type": "error", "message": message},
            {"type": "turn.failed", "error": {"message": message}},
            {"type": "item.completed", "item": {"type": "error", "message": message}},
        ]
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            reviewer = review.Reviewer("codex", "openai", "fake", "codex")
            for event in events:
                with self.subTest(event=event):
                    def fake_run_process(command, cwd, **kwargs):
                        pathlib.Path(command[command.index("--output-last-message") + 1]).write_text(
                            "# Review\n\nPlausible but uninspected output."
                        )
                        return subprocess.CompletedProcess(command, 0, json.dumps(event), "")

                    posix_os = mock.Mock(wraps=review.os)
                    posix_os.name = "posix"
                    with (
                        mock.patch.object(review, "executable_prefix", return_value=["codex"]),
                        mock.patch.object(review, "run_process", side_effect=fake_run_process),
                        mock.patch.object(review, "os", posix_os),
                    ):
                        result = review.execute_agent(reviewer, root, "review prompt", 20)
                    self.assertFalse(result.ok)
                    self.assertIn("runtime failure", result.error)

    def test_codex_error_1312_retries_in_disposable_snapshot_with_remaining_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            root.mkdir()
            self.init_repo(root)
            reviewer = review.Reviewer("codex", "openai", "gpt-5.6-sol", "codex")
            commands: list[list[str]] = []
            timeouts: list[int] = []

            def fake_run_process(command, cwd, **kwargs):
                del cwd
                commands.append(list(command))
                timeouts.append(kwargs["timeout"])
                if len(commands) == 1:
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        "# Review\n\nPlausible but uninspected output.",
                        "CreateProcessAsUserW failed: 1312 (A specified logon session does not exist.)",
                    )
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "# Review\n\nThe snapshot fallback inspected the copy successfully.",
                    "",
                )

            windows_os = mock.Mock(wraps=review.os)
            windows_os.name = "nt"
            with (
                mock.patch.object(review, "executable_prefix", return_value=["codex"]),
                mock.patch.object(review, "run_process", side_effect=fake_run_process),
                mock.patch.object(review, "os", windows_os),
                mock.patch.object(review.time, "monotonic", side_effect=[0.0, 7.0, 8.0]),
            ):
                result = review.execute_agent(reviewer, root, "review prompt", 10)

            self.assertTrue(result.ok, result.error)
            self.assertEqual([10, 3], timeouts)
            self.assertNotIn('windows.sandbox="unelevated"', commands[0])
            self.assertNotIn('windows.sandbox="unelevated"', commands[1])
            self.assertIn("danger-full-access", commands[1])
            self.assertIn("error 1312", result.sandbox_fallback)
            self.assertIn("Windows read-only sandbox attempt", result.stderr)


class ArtifactTests(unittest.TestCase):
    def test_utf8_prompt_loading_preserves_newlines_and_rejects_invalid_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            prompt = pathlib.Path(temp) / "prompt.md"
            prompt.write_bytes(b"first\r\nsecond\r\n")
            self.assertEqual("first\r\nsecond\r\n", review.read_utf8(prompt, "Prompt"))
            prompt.write_bytes(b"invalid: \xff")
            with self.assertRaisesRegex(review.ReviewError, "not valid UTF-8"):
                review.read_utf8(prompt, "Prompt")

    def test_review_and_summary_keep_distinct_latest_manifests(self) -> None:
        root = pathlib.Path("D:/reviews")
        self.assertEqual(root / "sample-latest.manifest.json", review.latest_review_manifest_path(root, "sample"))
        self.assertEqual(
            root / "sample-latest.summary.manifest.json",
            review.latest_summary_manifest_path(root, "sample"),
        )

    def test_partial_run_archives_success_and_replaces_failed_latest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output = pathlib.Path(temp) / "reviews"
            current = output / "runs" / "run-one"
            current.mkdir(parents=True)
            prompt = pathlib.Path(temp) / "prompt.md"
            prompt.write_text("review", encoding="utf-8")
            codex = review.Reviewer("codex", "openai", "gpt-5.6-sol", "codex")
            claude = review.Reviewer("claude", "anthropic", "opus", "claude")
            probes = [
                review.ProbeResult("codex", True, codex, "ok"),
                review.ProbeResult("claude", True, claude, "ok"),
            ]
            results = [
                review.AgentResult(codex, True, "# OpenAI review\n\nUseful finding.\n"),
                review.AgentResult(claude, False, error="provider stopped"),
            ]
            manifest = review.publish_review_artifacts(
                output, "sample", current, probes, results, prompt, "start"
            )
            self.assertEqual("partial", manifest["status"])
            self.assertIn("Useful finding", (output / "sample-latest.gpt.md").read_text())
            self.assertIn("provider stopped", (output / "sample-latest.claude.md").read_text())
            self.assertEqual(
                "partial",
                json.loads((output / "sample-latest.manifest.json").read_text())["status"],
            )

    def test_run_and_glob_input_selection(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            output = root / "plans" / "reviews"
            reports = output / "runs" / "abc-review" / "reports"
            reports.mkdir(parents=True)
            (reports / "one.md").write_text("one", encoding="utf-8")
            (output / "runs" / "abc-review" / "manifest.json").write_text(
                json.dumps({"name": "review"}), encoding="utf-8"
            )
            selected, name, missing = review.resolve_review_inputs(root, output, "abc", [], [])
            self.assertEqual("review", name)
            self.assertEqual([], missing)
            expected = (reports / "one.md").resolve()
            self.assertEqual([expected], selected)
            selected, _, _ = review.resolve_review_inputs(root, output, None, [], ["plans/reviews/runs/**/one.md"])
            self.assertEqual([expected], selected)

    def test_failure_text_is_not_accepted_as_review(self) -> None:
        with self.assertRaises(review.ReviewError):
            review.validate_output("You are not logged in; please sign in first", "provider")

        with self.assertRaisesRegex(review.ReviewError, "unable to complete"):
            review.validate_output(
                "Unable to complete the review because the sandbox denied process creation.",
                "provider",
            )

    def test_review_finding_can_say_permission_denied(self) -> None:
        output = review.validate_output(
            "# Finding\n\nThe application should surface permission denied errors.", "provider"
        )
        self.assertIn("permission denied", output)

    def test_progress_text_is_not_accepted_as_a_report(self) -> None:
        with self.assertRaisesRegex(review.ReviewError, "progress text"):
            review.validate_output(
                "I'll read the prompt first. Next I'll inspect the requested files.", "provider"
            )

    def test_grok_progress_prefix_is_removed_before_validation(self) -> None:
        output = review.provider_final_output(
            "I'll inspect the tree.Next I'll compare targets.# Review\n\n- Finding",
            "grok",
        )
        self.assertEqual("# Review\n\n- Finding", output)

    def test_plain_preamble_is_preserved_for_other_transports(self) -> None:
        output = review.provider_final_output(
            "Inspection notes.\n\n# Review\n\n- Finding",
            "claude",
        )
        self.assertTrue(output.startswith("Inspection notes."))

    def test_extract_kanban_cards_uses_consensus_headings_and_excludes_model_tag(self) -> None:
        summary = """# Consensus

### `kanban/pending/12 first-boundary -refactor.md`

# First boundary

- [ ] First task

### kanban/pending/13 second-boundary -refactor.md

# Second boundary

- [ ] Second task

<model>test-model</model>
"""
        cards = review.extract_kanban_cards(summary)
        self.assertEqual(
            ["12 first-boundary -refactor.md", "13 second-boundary -refactor.md"],
            [card.filename for card in cards],
        )
        self.assertNotIn("<model>", cards[-1].content)

    def test_materialize_kanban_cards_creates_validated_files_atomically(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            (root / "kanban" / "pending").mkdir(parents=True)
            summary = """### `kanban/pending/12 first-boundary -refactor.md`

# First boundary

- [ ] First task
"""
            created = review.materialize_kanban_cards(root, summary)
            target = root / "kanban" / "pending" / "12 first-boundary -refactor.md"
            self.assertTrue(target.is_file())
            self.assertEqual(
                str(pathlib.Path("kanban") / "pending" / "12 first-boundary -refactor.md"),
                created[0]["path"],
            )
            self.assertIn("First task", target.read_text(encoding="utf-8"))

    def test_materialize_remaps_collisions_to_lowest_free_priorities(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            pending = root / "kanban" / "pending"
            pending.mkdir(parents=True)
            (pending / "00 existing -feature.md").write_text("existing", encoding="utf-8")
            (pending / "02 another -feature.md").write_text("existing", encoding="utf-8")
            summary = """### `kanban/pending/00 first-boundary -refactor.md`

# First boundary

- [ ] First task
- [ ] Coordinate with `kanban/pending/02 collision -refactor.md`.

### `kanban/pending/02 collision -refactor.md`

# Collision

- [ ] Second task

Dependencies: `00 first-boundary -refactor.md`, `02 collision -refactor.md`.
"""
            normalized = review.normalize_kanban_summary(root, summary)
            self.assertIn("kanban/pending/01 first-boundary -refactor.md", normalized)
            self.assertIn("kanban/pending/03 collision -refactor.md", normalized)
            self.assertNotIn("kanban/pending/02 collision -refactor.md", normalized)
            self.assertIn("`01 first-boundary -refactor.md`", normalized)
            self.assertIn("`03 collision -refactor.md`", normalized)
            created = review.materialize_kanban_cards(root, normalized)
            self.assertEqual(
                [
                    str(pathlib.Path("kanban") / "pending" / "01 first-boundary -refactor.md"),
                    str(pathlib.Path("kanban") / "pending" / "03 collision -refactor.md"),
                ],
                [item["path"] for item in created],
            )

    def test_codex_session_recovery_extracts_last_final_answer(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            session = pathlib.Path(temp) / "session.jsonl"
            events = [
                {
                    "type": "response_item",
                    "payload": {
                        "type": "message",
                        "role": "assistant",
                        "phase": "final_answer",
                        "content": [{"type": "output_text", "text": "# First\n\n- old"}],
                    },
                },
                {
                    "type": "response_item",
                    "payload": {
                        "type": "message",
                        "role": "assistant",
                        "phase": "final_answer",
                        "content": [{"type": "output_text", "text": "# Recovered\n\n- final"}],
                    },
                },
            ]
            session.write_text(
                "\n".join(json.dumps(event) for event in events) + "\n",
                encoding="utf-8",
            )
            self.assertEqual("# Recovered\n\n- final\n", review.codex_session_final_answer(session))

    def test_materialize_rejects_unsafe_or_incomplete_cards(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            (root / "kanban" / "pending").mkdir(parents=True)
            unsafe = review.KanbanCard("../12 escape -refactor.md", "# Escape\n\n- [ ] Task\n")
            with self.assertRaisesRegex(review.ReviewError, "Unsafe"):
                review.validate_kanban_cards(root, [unsafe])
            incomplete = review.KanbanCard("12 no-checklist -refactor.md", "# No checklist\n")
            with self.assertRaisesRegex(review.ReviewError, "unchecked tasks"):
                review.validate_kanban_cards(root, [incomplete])

    def test_windows_sandbox_failure_in_diagnostics_rejects_plausible_output(self) -> None:
        with self.assertRaisesRegex(review.ReviewError, "runtime failure"):
            review.validate_runtime_diagnostics(
                "windows sandbox: runner failed during SpawnChild: CreateProcessAsUserW failed: 1312",
                "codex",
            )

    def test_diagnostic_secrets_are_redacted(self) -> None:
        sanitized = review.sanitize_diagnostics(
            "Authorization: Bearer secret-token-123 API_KEY=sk-example123456789012345"
        )
        self.assertNotIn("secret-token", sanitized)
        self.assertNotIn("sk-example", sanitized)
        self.assertEqual(2, sanitized.count("<redacted>"))

    def test_partial_run_selection_identifies_missing_reviewer(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            output = root / "plans" / "reviews"
            run = output / "runs" / "partial-run"
            reports = run / "reports"
            reports.mkdir(parents=True)
            (reports / "openai.codex.model.md").write_text("# Review\n\nSuccess", encoding="utf-8")
            (run / "manifest.json").write_text(
                json.dumps(
                    {
                        "name": "sample",
                        "status": "partial",
                        "reviewers": [
                            {
                                "company": "anthropic",
                                "transport": "claude",
                                "model": "opus",
                                "status": "failed",
                                "error": "timed out",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selected, name, missing = review.resolve_review_inputs(root, output, "partial", [], [])
            self.assertEqual("sample", name)
            self.assertEqual([(reports / "openai.codex.model.md").resolve()], selected)
            self.assertEqual(["anthropic/claude/opus: timed out"], missing)


if __name__ == "__main__":
    unittest.main()
