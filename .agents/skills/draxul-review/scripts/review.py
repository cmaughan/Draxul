#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import datetime as dt
import glob
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from collections.abc import Callable, Iterable, Sequence


DEFAULT_REVIEW_TIMEOUT = 1800
DEFAULT_PREFLIGHT_TIMEOUT = 60
FAILURE_PATTERNS = (
    "unable to complete",
    "not logged in",
    "please sign in",
    "failed to get oauth token",
    "authentication failed",
    "error authenticating:",
    "invalid api key",
)
RUNTIME_FAILURE_PATTERNS = (
    "createprocessasuserw failed: 1312",
    "a specified logon session does not exist",
    "windows sandbox: runner failed during spawnchild",
)
WINDOWS_LOGON_SESSION_PATTERNS = (
    "createprocessasuserw failed: 1312",
    "a specified logon session does not exist",
)
SENSITIVE_PATTERNS = (
    re.compile(r"(?i)(authorization\s*:\s*bearer\s+)[^\s]+"),
    re.compile(r"(?i)((?:api[_-]?key|access[_-]?token|refresh[_-]?token|client[_-]?secret)\s*[=:]\s*)[^\s,;]+"),
    re.compile(r"\b(?:sk-|xai-|AIza)[_A-Za-z0-9-]{16,}\b"),
)


@dataclasses.dataclass(frozen=True)
class Adapter:
    transport: str
    company: str
    command: str
    default_model: str
    latest_suffix: str


@dataclasses.dataclass(frozen=True)
class Reviewer:
    transport: str
    company: str
    model: str
    requested_transport: str
    fallback_reason: str = ""


@dataclasses.dataclass
class ProbeResult:
    requested: str
    ok: bool
    reviewer: Reviewer | None = None
    message: str = ""
    version: str = ""
    duration_seconds: float = 0.0


@dataclasses.dataclass
class AgentResult:
    reviewer: Reviewer
    ok: bool
    output: str = ""
    error: str = ""
    stderr: str = ""
    duration_seconds: float = 0.0
    sandbox_fallback: str = ""


@dataclasses.dataclass(frozen=True)
class KanbanCard:
    filename: str
    content: str


ADAPTERS = {
    "codex": Adapter("codex", "openai", "codex", "gpt-5.6-sol", "gpt"),
    "claude": Adapter("claude", "anthropic", "claude", "opus", "claude"),
    "agy": Adapter("agy", "google", "agy", "default", "gemini"),
    "gemini": Adapter("gemini", "google", "gemini", "default", "gemini"),
    "grok": Adapter("grok", "xai", "grok", "grok-4.5", "grok"),
}

DEFAULT_PANEL = ("codex", "claude", "google")
ALL_COMPANIES = ("codex", "claude", "google", "grok")


class ReviewError(RuntimeError):
    pass


def repo_root_from_script() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[4]


def slug(value: str, fallback: str = "review") -> str:
    normalized = re.sub(r"[^a-zA-Z0-9]+", "-", value).strip("-").lower()
    return normalized or fallback


def safe_component(value: str) -> str:
    return slug(value, "default")[:80]


def derive_name(prompt_file: pathlib.Path) -> str:
    stem = prompt_file.stem
    for prefix in ("consensus_", "consensus-", "summary_", "summary-"):
        if stem.startswith(prefix):
            stem = stem[len(prefix):]
            break
    return slug(stem)


def atomic_write(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    temporary.write_text(text, encoding="utf-8", newline="\n")
    os.replace(temporary, path)


def atomic_write_json(path: pathlib.Path, value: object) -> None:
    atomic_write(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def atomic_create(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.link(temporary, path)
    except FileExistsError as error:
        raise ReviewError(f"Refusing to overwrite existing Kanban work item: {path}") from error
    finally:
        temporary.unlink(missing_ok=True)


def read_utf8(path: pathlib.Path, label: str) -> str:
    try:
        return path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as error:
        raise ReviewError(f"{label} is not valid UTF-8: {path}") from error


def resolve_input_path(value: str, root: pathlib.Path, must_exist: bool = True) -> pathlib.Path:
    path = pathlib.Path(value).expanduser()
    if not path.is_absolute():
        path = root / path
    path = path.resolve()
    if must_exist and not path.exists():
        raise ReviewError(f"Path does not exist: {path}")
    return path


def parse_reviewer(value: str) -> tuple[str, str]:
    transport, separator, model = value.partition(":")
    transport = transport.strip().lower()
    model = model.strip() if separator else ""
    if transport not in {*ADAPTERS, "google"}:
        raise ReviewError(
            f"Unsupported reviewer '{transport}'. Expected codex, claude, google, agy, gemini, or grok."
        )
    return transport, model


def company_for_requested(transport: str) -> str:
    return "google" if transport == "google" else ADAPTERS[transport].company


def validate_company_uniqueness(requests: Sequence[tuple[str, str]]) -> None:
    seen: dict[str, str] = {}
    for transport, _ in requests:
        company = company_for_requested(transport)
        if company in seen:
            raise ReviewError(
                f"Reviewers '{seen[company]}' and '{transport}' both belong to {company}; "
                "choose at most one reviewer per company."
            )
        seen[company] = transport


def requested_panel(values: Sequence[str], use_all: bool) -> list[tuple[str, str]]:
    if values and use_all:
        raise ReviewError("Use either --reviewer or --all, not both.")
    raw = list(ALL_COMPANIES if use_all else DEFAULT_PANEL if not values else values)
    if not 1 <= len(raw) <= 4:
        raise ReviewError("A review panel must contain between one and four reviewers.")
    requests = [parse_reviewer(value) for value in raw]
    validate_company_uniqueness(requests)
    return requests


def executable_prefix(command: str) -> list[str]:
    resolved = shutil.which(command)
    if not resolved:
        raise ReviewError(f"{command} CLI is not on PATH.")
    suffix = pathlib.Path(resolved).suffix.lower()
    if os.name == "nt" and suffix == ".ps1":
        powershell = shutil.which("powershell.exe") or shutil.which("pwsh.exe")
        if not powershell:
            raise ReviewError(f"{command} resolves to a PowerShell script but PowerShell is unavailable.")
        return [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", resolved]
    if os.name == "nt" and suffix in {".cmd", ".bat"}:
        return [os.environ.get("COMSPEC", "cmd.exe"), "/d", "/s", "/c", resolved]
    return [resolved]


def run_process(
    command: Sequence[str],
    cwd: pathlib.Path,
    *,
    input_text: str | None = None,
    timeout: int,
    env_overrides: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["PYTHONUNBUFFERED"] = "1"
    if env_overrides:
        env.update(env_overrides)
    return subprocess.run(
        list(command),
        cwd=cwd,
        input=input_text,
        text=True,
        capture_output=True,
        check=False,
        timeout=timeout,
        encoding="utf-8",
        errors="strict",
        env=env,
    )


def model_args(adapter: Adapter, model: str) -> list[str]:
    if not model or model == "default":
        return []
    if adapter.transport == "codex":
        return ["--model", model]
    if adapter.transport == "claude":
        return ["--model", model]
    if adapter.transport == "agy":
        return ["--model", model]
    if adapter.transport == "gemini":
        return ["--model", model]
    if adapter.transport == "grok":
        return ["--model", model]
    return []


def version_command(adapter: Adapter) -> list[str]:
    return [*executable_prefix(adapter.command), "--version"]


def auth_command(adapter: Adapter) -> list[str] | None:
    prefix = executable_prefix(adapter.command)
    if adapter.transport == "codex":
        return [*prefix, "login", "status"]
    if adapter.transport == "claude":
        return [*prefix, "auth", "status"]
    if adapter.transport == "agy":
        return [*prefix, "models"]
    if adapter.transport == "grok":
        return [*prefix, "models"]
    return None


def agent_command(
    reviewer: Reviewer,
    cwd: pathlib.Path,
    prompt: str,
    output_file: pathlib.Path,
    timeout: int,
    windows_sandbox: str | None = None,
    sandbox_mode: str = "read-only",
    persist_session: bool = True,
) -> tuple[list[str], str | None]:
    adapter = ADAPTERS[reviewer.transport]
    prefix = executable_prefix(adapter.command)
    args = model_args(adapter, reviewer.model)
    if reviewer.transport == "codex":
        sandbox_override = (
            ["--config", f'windows.sandbox="{windows_sandbox}"']
            if windows_sandbox
            else []
        )
        session_args = [] if persist_session else ["--ephemeral"]
        command = [
            *prefix,
            "--disable",
            "apps",
            "--disable",
            "browser_use",
            "--disable",
            "browser_use_external",
            "--disable",
            "plugins",
            "--disable",
            "multi_agent",
            "--config",
            "mcp_servers={}",
            *sandbox_override,
            "--ask-for-approval",
            "never",
            "exec",
            "--skip-git-repo-check",
            "--cd",
            str(cwd),
            *args,
            "--sandbox",
            sandbox_mode,
            *session_args,
            "--output-last-message",
            str(output_file),
            "-",
        ]
        return command, prompt
    if reviewer.transport == "claude":
        session_args = [] if persist_session else ["--no-session-persistence"]
        command = [
            *prefix,
            "--print",
            *args,
            "--output-format",
            "text",
            "--permission-mode",
            "plan",
            "--allowedTools",
            "Read,Grep,Glob",
            "--strict-mcp-config",
            "--mcp-config",
            '{"mcpServers":{}}',
            *session_args,
        ]
        return command, prompt
    if reviewer.transport == "agy":
        command = [
            *prefix,
            *args,
            "--print",
            prompt,
            "--print-timeout",
            f"{timeout}s",
            "--sandbox",
        ]
        return command, None
    if reviewer.transport == "gemini":
        command = [
            *prefix,
            *args,
            "--prompt",
            prompt,
            "--approval-mode",
            "plan",
            "--output-format",
            "text",
            "--skip-trust",
        ]
        return command, None
    if reviewer.transport == "grok":
        command = [
            *prefix,
            "--single",
            prompt,
            "--cwd",
            str(cwd),
            *args,
            "--output-format",
            "plain",
            "--permission-mode",
            "plan",
            "--tools",
            "Read,Grep,Glob",
            "--no-subagents",
            "--disable-web-search",
            "--no-memory",
        ]
        return command, None
    raise ReviewError(f"Unsupported reviewer transport: {reviewer.transport}")


def clean_output(text: str) -> str:
    ansi = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    cleaned = ansi.sub("", text).replace("\r\n", "\n").strip()
    ignored = {
        "YOLO mode is enabled. All tool calls will be automatically approved.",
        "Loaded cached credentials.",
    }
    lines = [line for line in cleaned.splitlines() if line.strip() not in ignored]
    return "\n".join(lines).strip()


def provider_final_output(text: str, transport: str) -> str:
    cleaned = clean_output(text)
    if transport == "grok":
        first_heading = re.search(r"#{1,6}\s+", cleaned)
        if first_heading and cleaned[: first_heading.start()].strip():
            return cleaned[first_heading.start() :]
    return cleaned


def sanitize_diagnostics(text: str) -> str:
    cleaned = clean_output(text)
    for pattern in SENSITIVE_PATTERNS:
        cleaned = pattern.sub(lambda match: (match.group(1) if match.lastindex else "") + "<redacted>", cleaned)
    return cleaned


def validate_output(text: str, label: str) -> str:
    cleaned = clean_output(text)
    if len(cleaned) < 20:
        raise ReviewError(f"{label} produced no meaningful Markdown output.")
    lowered = cleaned.lower()
    markdown_structure = bool(re.search(r"(?m)^(?:#{1,6}\s|[-*+]\s|\d+\.\s|```|\|)", cleaned))
    for pattern in FAILURE_PATTERNS:
        if pattern in lowered and (
            not markdown_structure or lowered.startswith(("error", "unable", "you are", "please"))
        ):
            raise ReviewError(f"{label} output looks like a provider failure ({pattern}).")
    if not markdown_structure and re.search(r"(?i)\b(?:i(?:'|’)ll|next i(?:'|’)ll)\s+(?:read|inspect|check|review)\b", cleaned):
        raise ReviewError(f"{label} returned progress text instead of a final Markdown report.")
    return cleaned + "\n"


def validate_runtime_diagnostics(text: str, label: str) -> None:
    lowered = clean_output(text).lower()
    for pattern in RUNTIME_FAILURE_PATTERNS:
        if pattern in lowered:
            raise ReviewError(f"{label} encountered a provider runtime failure ({pattern}).")


def is_windows_logon_session_failure(text: str) -> bool:
    lowered = clean_output(text).lower()
    return any(pattern in lowered for pattern in WINDOWS_LOGON_SESSION_PATTERNS)


def isolated_grok_environment(temp_root: pathlib.Path) -> dict[str, str]:
    source_home = pathlib.Path.home() / ".grok"
    isolated_home = temp_root / "grok-home"
    isolated_home.mkdir(parents=True, exist_ok=True)
    for name in ("auth.json", "agent_id", "models_cache.json", ".metadata_version"):
        source = source_home / name
        if source.is_file():
            shutil.copy2(source, isolated_home / name)
    return {"GROK_HOME": str(isolated_home)}


def live_probe(reviewer: Reviewer, timeout: int) -> tuple[bool, str]:
    nonce = f"DRAXUL_PREFLIGHT_{uuid.uuid4().hex[:12].upper()}"
    prompt = f"Reply with exactly {nonce}. Do not use tools and do not add punctuation."
    with tempfile.TemporaryDirectory(prefix="draxul-preflight-") as temp:
        cwd = pathlib.Path(temp)
        output_file = cwd / "response.txt"
        command, input_text = agent_command(
            reviewer,
            cwd,
            prompt,
            output_file,
            timeout,
            persist_session=False,
        )
        try:
            environment = isolated_grok_environment(cwd) if reviewer.transport == "grok" else None
            result = run_process(
                command, cwd, input_text=input_text, timeout=timeout, env_overrides=environment
            )
        except subprocess.TimeoutExpired:
            return False, f"live probe timed out after {timeout}s"
        output = read_utf8(output_file, "Provider output") if output_file.exists() else result.stdout
        combined = sanitize_diagnostics(output + "\n" + result.stderr)
        if result.returncode != 0:
            return False, combined or f"live probe exited with {result.returncode}"
        if nonce not in combined:
            return False, f"live probe did not return the expected nonce; output was: {combined[:240]}"
    return True, "live response verified"


def probe_transport(transport: str, model: str, timeout: int) -> ProbeResult:
    started = time.monotonic()
    adapter = ADAPTERS[transport]
    effective_model = model or adapter.default_model
    reviewer = Reviewer(transport, adapter.company, effective_model, transport)
    try:
        version = run_process(version_command(adapter), pathlib.Path.cwd(), timeout=min(timeout, 20))
        version_text = clean_output(version.stdout + "\n" + version.stderr)
        if version.returncode != 0:
            raise ReviewError(version_text or f"version check exited with {version.returncode}")
        auth = auth_command(adapter)
        if auth is not None:
            auth_result = run_process(auth, pathlib.Path.cwd(), timeout=min(timeout, 30))
            auth_text = sanitize_diagnostics(auth_result.stdout + "\n" + auth_result.stderr)
            if auth_result.returncode != 0:
                raise ReviewError(auth_text or f"authentication check exited with {auth_result.returncode}")
        live_ok, live_message = live_probe(reviewer, timeout)
        if not live_ok:
            raise ReviewError(live_message)
        return ProbeResult(
            requested=transport,
            ok=True,
            reviewer=reviewer,
            message=live_message,
            version=version_text.splitlines()[0] if version_text else "",
            duration_seconds=time.monotonic() - started,
        )
    except (ReviewError, subprocess.TimeoutExpired, UnicodeError, OSError) as error:
        return ProbeResult(
            requested=transport,
            ok=False,
            reviewer=reviewer,
            message=str(error),
            duration_seconds=time.monotonic() - started,
        )


ProbeFunction = Callable[[str, str, int], ProbeResult]


def resolve_and_probe(
    request: tuple[str, str],
    timeout: int,
    probe_fn: ProbeFunction = probe_transport,
) -> tuple[ProbeResult, list[ProbeResult]]:
    transport, model = request
    if transport != "google":
        result = probe_fn(transport, model, timeout)
        return result, [result]

    agy = probe_fn("agy", model, timeout)
    if agy.ok and agy.reviewer is not None:
        selected = dataclasses.replace(agy.reviewer, requested_transport="google")
        result = dataclasses.replace(agy, requested="google", reviewer=selected)
        return result, [agy]

    gemini = probe_fn("gemini", model, timeout)
    if gemini.ok and gemini.reviewer is not None:
        reason = f"Agy unavailable: {agy.message}"
        selected = dataclasses.replace(
            gemini.reviewer,
            requested_transport="google",
            fallback_reason=reason,
        )
        result = dataclasses.replace(
            gemini,
            requested="google",
            reviewer=selected,
            message=f"using Gemini fallback; {reason}",
        )
        return result, [agy, gemini]

    result = ProbeResult(
        requested="google",
        ok=False,
        message=f"Agy failed: {agy.message}; Gemini failed: {gemini.message}",
        duration_seconds=agy.duration_seconds + gemini.duration_seconds,
    )
    return result, [agy, gemini]


def git_output(root: pathlib.Path, args: Sequence[str], *, binary: bool = False) -> str | bytes:
    command = ["git", "-C", str(root), *args]
    result = subprocess.run(command, capture_output=True, check=False)
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise ReviewError(message or f"git {' '.join(args)} failed")
    if binary:
        return result.stdout
    return result.stdout.decode("utf-8", errors="replace")


def repository_state(root: pathlib.Path) -> str:
    sections = [
        ("Branch", str(git_output(root, ["branch", "--show-current"]))),
        ("HEAD", str(git_output(root, ["rev-parse", "HEAD"]))),
        ("Status", str(git_output(root, ["status", "--short"]))),
        ("Unstaged diff", str(git_output(root, ["diff", "--no-ext-diff", "--no-color"]))),
        ("Staged diff", str(git_output(root, ["diff", "--cached", "--no-ext-diff", "--no-color"]))),
    ]
    body = ["# Repository state", ""]
    for title, value in sections:
        body.extend([f"## {title}", "", "```text", value.rstrip(), "```", ""])
    return "\n".join(body)


def snapshot_file_list(root: pathlib.Path) -> list[pathlib.Path]:
    raw = git_output(root, ["ls-files", "-z", "--cached", "--others", "--exclude-standard"], binary=True)
    assert isinstance(raw, bytes)
    paths: list[pathlib.Path] = []
    for encoded in raw.split(b"\0"):
        if not encoded:
            continue
        relative = pathlib.Path(encoded.decode("utf-8", errors="surrogateescape"))
        normalized = relative.as_posix()
        if normalized.startswith("plans/reviews/"):
            continue
        source = root / relative
        if source.is_file() or source.is_symlink():
            paths.append(relative)
    return paths


def copy_source_snapshot(root: pathlib.Path, destination: pathlib.Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for relative in snapshot_file_list(root):
        source = root / relative
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_symlink():
            resolved = source.resolve()
            try:
                resolved.relative_to(root.resolve())
            except ValueError as error:
                raise ReviewError(f"Refusing repository symlink outside the checkout: {relative}") from error
            if not resolved.is_file():
                raise ReviewError(f"Refusing non-file repository symlink: {relative}")
            shutil.copy2(resolved, target)
        else:
            shutil.copy2(source, target)
    atomic_write(destination / "REPO_STATE.md", repository_state(root))


def review_bootstrap(summary: bool = False) -> str:
    task = "SYNTHESIS_PROMPT.md" if summary else "REVIEW_PROMPT.md"
    extra = ""
    if summary:
        extra = (
            "Read INPUT_REVIEWS/index.md and every listed review before responding. "
            "If SYNTHESIS_PROMPT.md requests Kanban work items, include each complete card in "
            "the final response under an exact level-three heading of the form "
            "`### kanban/pending/<filename>.md`; the trusted parent process will validate and "
            "create those files. Do not claim that you created them. "
        )
    return (
        f"Read {task} and follow it exactly. {extra}"
        "This is a review-only task in a disposable repository snapshot. "
        "Inspect files but do not edit, create, delete, format, build, install, or run project binaries. "
        "Return the complete result as minimal Markdown in your final response. "
        "Do not write the report to a file."
    )


def execute_agent(
    reviewer: Reviewer,
    root: pathlib.Path,
    prompt_text: str,
    timeout: int,
    *,
    summary_inputs: Sequence[pathlib.Path] = (),
    summary: bool = False,
) -> AgentResult:
    started = time.monotonic()
    label = f"{reviewer.company}/{reviewer.transport}:{reviewer.model}"
    rejected_output = ""
    diagnostic_stderr = ""
    sandbox_fallback = ""
    try:
        with tempfile.TemporaryDirectory(prefix=f"draxul-review-{reviewer.transport}-") as temp:
            workspace = pathlib.Path(temp) / "workspace"
            copy_source_snapshot(root, workspace)
            prompt_name = "SYNTHESIS_PROMPT.md" if summary else "REVIEW_PROMPT.md"
            atomic_write(workspace / prompt_name, prompt_text)
            if summary:
                inputs_dir = workspace / "INPUT_REVIEWS"
                index_lines = ["# Input reviews", ""]
                for index, source in enumerate(summary_inputs, 1):
                    target_name = f"{index:02d}-{safe_component(source.stem)}.md"
                    target = inputs_dir / target_name
                    atomic_write(target, read_utf8(source, "Review input"))
                    digest = hashlib.sha256(source.read_bytes()).hexdigest()
                    index_lines.append(
                        f"- `{target_name}` — source `{source.name}` — sha256 `{digest}`"
                    )
                atomic_write(inputs_dir / "index.md", "\n".join(index_lines) + "\n")
            environment = None

            def run_attempt(
                output_file: pathlib.Path,
                attempt_timeout: int,
                windows_sandbox: str | None = None,
                sandbox_mode: str = "read-only",
            ) -> tuple[subprocess.CompletedProcess[str], str]:
                command, input_text = agent_command(
                    reviewer,
                    workspace,
                    review_bootstrap(summary),
                    output_file,
                    attempt_timeout,
                    windows_sandbox=windows_sandbox,
                    sandbox_mode=sandbox_mode,
                )
                completed = run_process(
                    command,
                    workspace,
                    input_text=input_text,
                    timeout=attempt_timeout,
                    env_overrides=environment,
                )
                response = (
                    read_utf8(output_file, "Provider output")
                    if output_file.exists()
                    else completed.stdout
                )
                return completed, response

            output_file = workspace / "AGENT_OUTPUT.md"
            result, output = run_attempt(output_file, timeout)
            diagnostic_stderr = result.stderr
            first_diagnostics = "\n".join((result.stdout, result.stderr, output))
            if (
                reviewer.transport == "codex"
                and os.name == "nt"
                and is_windows_logon_session_failure(first_diagnostics)
            ):
                elapsed = time.monotonic() - started
                remaining = max(0, int(timeout - elapsed))
                if remaining == 0:
                    raise ReviewError(
                        f"{label} exhausted its timeout after Windows sandbox error 1312."
                    )
                sandbox_fallback = (
                    "Windows sandbox failed to create a child process with error 1312; "
                    "retried without the OS sandbox inside the disposable repository snapshot. "
                    "The review-only prompt and snapshot boundary remained in force."
                )
                retry_file = workspace / "AGENT_OUTPUT_SNAPSHOT_FALLBACK.md"
                result, output = run_attempt(
                    retry_file,
                    remaining,
                    sandbox_mode="danger-full-access",
                )
                diagnostic_stderr = (
                    "Windows read-only sandbox attempt:\n"
                    f"{diagnostic_stderr}\n\nDisposable snapshot fallback:\n{result.stderr}"
                )
            if result.returncode != 0:
                error_text = clean_output(result.stderr or result.stdout)
                raise ReviewError(error_text or f"agent exited with {result.returncode}")
            validate_runtime_diagnostics(result.stderr, label)
            try:
                validated = validate_output(
                    provider_final_output(output, reviewer.transport),
                    label,
                )
            except ReviewError:
                rejected_output = output
                raise
            return AgentResult(
                reviewer=reviewer,
                ok=True,
                output=validated,
                stderr=sanitize_diagnostics(diagnostic_stderr),
                duration_seconds=time.monotonic() - started,
                sandbox_fallback=sandbox_fallback,
            )
    except (ReviewError, subprocess.TimeoutExpired, UnicodeError, OSError) as error:
        diagnostic = diagnostic_stderr
        if rejected_output:
            diagnostic = f"{diagnostic}\nRejected output:\n{rejected_output}"
        return AgentResult(
            reviewer=reviewer,
            ok=False,
            error=sanitize_diagnostics(str(error)),
            stderr=sanitize_diagnostics(diagnostic),
            duration_seconds=time.monotonic() - started,
            sandbox_fallback=sandbox_fallback,
        )


def run_id(name: str) -> str:
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"{stamp}-{slug(name)}-{uuid.uuid4().hex[:8]}"


def manifest_reviewer(probe: ProbeResult, result: AgentResult | None = None) -> dict[str, object]:
    reviewer = result.reviewer if result is not None else probe.reviewer
    return {
        "requested": probe.requested,
        "company": reviewer.company if reviewer else company_for_requested(parse_reviewer(probe.requested)[0]),
        "transport": reviewer.transport if reviewer else None,
        "model": reviewer.model if reviewer else None,
        "fallback_reason": reviewer.fallback_reason if reviewer else "",
        "sandbox_fallback": result.sandbox_fallback if result else "",
        "preflight": "passed" if probe.ok else "failed",
        "preflight_message": probe.message,
        "version": probe.version,
        "status": "passed" if result and result.ok else "failed" if result else "not-run",
        "error": result.error if result else "",
        "duration_seconds": round(result.duration_seconds, 3) if result else None,
    }


def report_filename(reviewer: Reviewer) -> str:
    return ".".join(
        [safe_component(reviewer.company), safe_component(reviewer.transport), safe_component(reviewer.model)]
    ) + ".md"


def latest_review_manifest_path(output_root: pathlib.Path, name: str) -> pathlib.Path:
    return output_root / f"{name}-latest.manifest.json"


def latest_summary_manifest_path(output_root: pathlib.Path, name: str) -> pathlib.Path:
    return output_root / f"{name}-latest.summary.manifest.json"


KANBAN_CARD_HEADING = re.compile(
    r"(?m)^###\s+`?kanban/pending/(?P<filename>[^`\r\n]+\.md)`?\s*$"
)
KANBAN_CARD_FILENAME = re.compile(
    r"^(?P<priority>\d{2}) [a-z0-9][a-z0-9 -]* -(?:bug|feature|refactor|architecture|test)\.md$"
)


def extract_kanban_cards(summary: str) -> list[KanbanCard]:
    matches = list(KANBAN_CARD_HEADING.finditer(summary))
    cards: list[KanbanCard] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(summary)
        content = summary[match.end() : end].strip()
        if index + 1 == len(matches):
            content = re.sub(r"\n*<model>[^\r\n]*</model>\s*$", "", content).rstrip()
        cards.append(KanbanCard(match.group("filename").strip(), content + "\n"))
    return cards


def validate_kanban_card_content(cards: Sequence[KanbanCard]) -> None:
    proposed_names: set[str] = set()
    for card in cards:
        if pathlib.Path(card.filename).name != card.filename or "/" in card.filename or "\\" in card.filename:
            raise ReviewError(f"Unsafe Kanban work-item filename: {card.filename}")
        if not KANBAN_CARD_FILENAME.fullmatch(card.filename):
            raise ReviewError(f"Invalid Kanban work-item filename: {card.filename}")
        if card.filename in proposed_names:
            raise ReviewError(f"Duplicate proposed Kanban work item: {card.filename}")
        if not re.search(r"(?m)^#\s+\S", card.content):
            raise ReviewError(f"Kanban work item lacks a title heading: {card.filename}")
        if not re.search(r"(?m)^- \[ \]\s+\S", card.content):
            raise ReviewError(f"Kanban work item lacks unchecked tasks: {card.filename}")
        proposed_names.add(card.filename)


def pending_priorities(root: pathlib.Path) -> set[int]:
    pending = root / "kanban" / "pending"
    return {
        int(match.group("priority"))
        for path in pending.glob("*.md")
        if (match := re.match(r"^(?P<priority>\d{2})\s", path.name))
    }


def normalize_kanban_summary(root: pathlib.Path, summary: str) -> str:
    cards = extract_kanban_cards(summary)
    validate_kanban_card_content(cards)
    if not cards:
        return summary

    occupied = pending_priorities(root)
    proposed = [int(KANBAN_CARD_FILENAME.fullmatch(card.filename).group("priority")) for card in cards]
    has_collision = bool(occupied.intersection(proposed)) or len(set(proposed)) != len(proposed)
    if not has_collision:
        return summary

    available = (priority for priority in range(100) if priority not in occupied)
    replacements: list[tuple[str, str]] = []
    for card in cards:
        try:
            priority = next(available)
        except StopIteration as error:
            raise ReviewError("No free two-digit Kanban pending priorities remain.") from error
        replacements.append((card.filename, f"{priority:02d}{card.filename[2:]}"))

    normalized = summary
    for old_name, new_name in replacements:
        normalized = normalized.replace(old_name, new_name)
    return normalized


def validate_kanban_cards(root: pathlib.Path, cards: Sequence[KanbanCard]) -> None:
    validate_kanban_card_content(cards)
    pending = root / "kanban" / "pending"
    existing_priorities = {f"{priority:02d}" for priority in pending_priorities(root)}
    proposed_priorities: set[str] = set()
    for card in cards:
        match = KANBAN_CARD_FILENAME.fullmatch(card.filename)
        assert match is not None
        priority = match.group("priority")
        target = pending / card.filename
        if target.exists():
            raise ReviewError(f"Refusing to overwrite existing Kanban work item: {target}")
        if priority in existing_priorities or priority in proposed_priorities:
            raise ReviewError(f"Kanban pending priority {priority} is already in use.")
        proposed_priorities.add(priority)


def materialize_kanban_cards(root: pathlib.Path, summary: str) -> list[dict[str, str]]:
    cards = extract_kanban_cards(normalize_kanban_summary(root, summary))
    validate_kanban_cards(root, cards)
    created: list[pathlib.Path] = []
    try:
        for card in cards:
            target = root / "kanban" / "pending" / card.filename
            atomic_create(target, card.content)
            created.append(target)
    except Exception:
        for target in created:
            target.unlink(missing_ok=True)
        raise
    return [
        {
            "path": str(path.relative_to(root)),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
        for path in created
    ]


def publish_review_artifacts(
    output_root: pathlib.Path,
    name: str,
    current_run: pathlib.Path,
    probes: Sequence[ProbeResult],
    results: Sequence[AgentResult],
    prompt_path: pathlib.Path,
    started_at: str,
) -> dict[str, object]:
    by_company = {result.reviewer.company: result for result in results}
    reports_dir = current_run / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, object]] = []
    all_ok = True
    for probe in probes:
        result = by_company.get(probe.reviewer.company) if probe.reviewer else None
        entry = manifest_reviewer(probe, result)
        entries.append(entry)
        company = str(entry["company"])
        suffix = next((adapter.latest_suffix for adapter in ADAPTERS.values() if adapter.company == company), company)
        stable = output_root / f"{name}-latest.{suffix}.md"
        if result and result.ok:
            archive = reports_dir / report_filename(result.reviewer)
            atomic_write(archive, result.output)
            atomic_write(stable, result.output)
            entry["report"] = str(archive.relative_to(output_root))
            entry["sha256"] = hashlib.sha256(result.output.encode("utf-8")).hexdigest()
        else:
            all_ok = False
            reason = result.error if result else probe.message
            stub = f"# {company.title()} review failed\n\nRun: `{current_run.name}`\n\nReason: {reason}\n"
            atomic_write(stable, stub)
    manifest = {
        "schema_version": 1,
        "kind": "review",
        "run_id": current_run.name,
        "name": name,
        "status": "complete" if all_ok else "partial",
        "started_at": started_at,
        "finished_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "prompt": str(prompt_path),
        "reviewers": entries,
    }
    atomic_write_json(current_run / "manifest.json", manifest)
    atomic_write_json(latest_review_manifest_path(output_root, name), manifest)
    return manifest


def resolve_review_inputs(
    root: pathlib.Path,
    output_root: pathlib.Path,
    run: str | None,
    inputs: Sequence[str],
    patterns: Sequence[str],
) -> tuple[list[pathlib.Path], str | None, list[str]]:
    selected: list[pathlib.Path] = []
    source_name: str | None = None
    missing_reviewers: list[str] = []
    if run:
        runs_dir = output_root / "runs"
        matches = [path for path in runs_dir.glob(f"{run}*") if path.is_dir()]
        if len(matches) != 1:
            raise ReviewError(f"Run selector '{run}' matched {len(matches)} runs; expected exactly one.")
        manifest_path = matches[0] / "manifest.json"
        if manifest_path.exists():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            source_name = str(manifest.get("name") or "review")
            if manifest.get("status") != "complete":
                for reviewer in manifest.get("reviewers", []):
                    if reviewer.get("status") != "passed":
                        label = "/".join(
                            str(value)
                            for value in (
                                reviewer.get("company"),
                                reviewer.get("transport"),
                                reviewer.get("model"),
                            )
                            if value
                        )
                        reason = reviewer.get("error") or reviewer.get("preflight_message") or "not available"
                        missing_reviewers.append(f"{label or reviewer.get('requested')}: {reason}")
        selected.extend(sorted((matches[0] / "reports").glob("*.md")))
    for value in inputs:
        selected.append(resolve_input_path(value, root))
    for pattern in patterns:
        absolute_pattern = pattern if pathlib.Path(pattern).is_absolute() else str(root / pattern)
        selected.extend(pathlib.Path(path).resolve() for path in glob.glob(absolute_pattern, recursive=True))
    unique: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for path in selected:
        if path.is_file() and path not in seen:
            seen.add(path)
            unique.append(path)
    if not unique:
        raise ReviewError("No review input files were selected.")
    return unique, source_name, missing_reviewers


def write_logs(current_run: pathlib.Path, results: Sequence[AgentResult]) -> None:
    for result in results:
        if result.stderr:
            atomic_write(current_run / "logs" / f"{result.reviewer.transport}.stderr.log", result.stderr + "\n")
        if result.error:
            atomic_write(current_run / "logs" / f"{result.reviewer.transport}.error.log", result.error + "\n")


def command_review(args: argparse.Namespace) -> int:
    root = resolve_input_path(args.repo_root, pathlib.Path.cwd())
    output_root = resolve_input_path(args.output_root, root, must_exist=False)
    prompt_path = resolve_input_path(args.prompt_file, root)
    prompt_text = read_utf8(prompt_path, "Prompt")
    name = slug(args.name or derive_name(prompt_path))
    requests = requested_panel(args.reviewer, args.all)
    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    current_run = output_root / "runs" / run_id(name)
    current_run.mkdir(parents=True, exist_ok=False)
    atomic_write(current_run / "prompt.md", prompt_text)

    probes: list[ProbeResult] = []
    for request in requests:
        probe, attempts = resolve_and_probe(request, args.preflight_timeout)
        probes.append(probe)
        for attempt in attempts:
            state = "ok" if attempt.ok else "failed"
            print(f"preflight {attempt.requested}: {state} — {attempt.message}", flush=True)

    runnable = [probe.reviewer for probe in probes if probe.ok and probe.reviewer is not None]
    if args.all:
        if len({reviewer.company for reviewer in runnable}) < 3:
            publish_review_artifacts(output_root, name, current_run, probes, [], prompt_path, started_at)
            raise ReviewError("Fewer than three companies passed preflight; no reviews were launched.")
    elif len(runnable) != len(probes):
        publish_review_artifacts(output_root, name, current_run, probes, [], prompt_path, started_at)
        raise ReviewError("A required reviewer failed preflight; no reviews were launched.")

    print(f"running {len(runnable)} independent reviews in parallel", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(runnable)) as executor:
        futures: dict[concurrent.futures.Future[AgentResult], Reviewer] = {}
        for reviewer in runnable:
            print(
                f"started {reviewer.company}/{reviewer.transport}:{reviewer.model} "
                f"(timeout {args.timeout}s)",
                flush=True,
            )
            future = executor.submit(execute_agent, reviewer, root, prompt_text, args.timeout)
            futures[future] = reviewer
        completed: dict[tuple[str, str], AgentResult] = {}
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            completed[(result.reviewer.transport, result.reviewer.model)] = result
            state = "ok" if result.ok else f"failed — {result.error}"
            print(
                f"completed {result.reviewer.company}/{result.reviewer.transport}: {state} "
                f"({result.duration_seconds:.1f}s)",
                flush=True,
            )
        results = [completed[(reviewer.transport, reviewer.model)] for reviewer in runnable]
    write_logs(current_run, results)
    manifest = publish_review_artifacts(
        output_root, name, current_run, probes, results, prompt_path, started_at
    )
    print(f"run: {current_run}", flush=True)
    return 0 if manifest["status"] == "complete" else 1


def publish_summary_artifacts(
    root: pathlib.Path,
    output_root: pathlib.Path,
    current_run: pathlib.Path,
    name: str,
    prompt_path: pathlib.Path,
    prompt_text: str,
    inputs: Sequence[pathlib.Path],
    missing_reviewers: Sequence[str],
    probe: ProbeResult,
    result: AgentResult,
    started_at: str,
    extra_manifest: dict[str, object] | None = None,
) -> dict[str, object]:
    if result.ok and missing_reviewers:
        missing = "\n".join(f"- {item}" for item in missing_reviewers)
        result.output = f"# Partial synthesis\n\nMissing reviewers:\n\n{missing}\n\n{result.output}"
    work_items: list[dict[str, str]] = []
    if result.ok and "kanban/pending/" in prompt_text.lower():
        result.output = normalize_kanban_summary(root, result.output)
        work_items = materialize_kanban_cards(root, result.output)
        for item in work_items:
            print(f"created work item {item['path']}", flush=True)
    if result.stderr:
        atomic_write(current_run / "logs" / f"{result.reviewer.transport}.stderr.log", result.stderr + "\n")
    if result.ok:
        atomic_write(current_run / "summary.md", result.output)
        atomic_write(output_root / f"{name}-consensus.md", result.output)
    manifest = {
        "schema_version": 1,
        "kind": "summary",
        "run_id": current_run.name,
        "name": name,
        "status": "partial" if result.ok and missing_reviewers else "complete" if result.ok else "failed",
        "started_at": started_at,
        "finished_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "prompt": str(prompt_path),
        "inputs": [str(path) for path in inputs],
        "missing_reviewers": list(missing_reviewers),
        "work_items": work_items,
        "summarizer": manifest_reviewer(probe, result),
    }
    if extra_manifest:
        manifest.update(extra_manifest)
    atomic_write_json(current_run / "manifest.json", manifest)
    atomic_write_json(latest_summary_manifest_path(output_root, name), manifest)
    return manifest


def command_summarize(args: argparse.Namespace) -> int:
    root = resolve_input_path(args.repo_root, pathlib.Path.cwd())
    output_root = resolve_input_path(args.output_root, root, must_exist=False)
    prompt_path = resolve_input_path(args.prompt_file, root)
    prompt_text = read_utf8(prompt_path, "Prompt")
    inputs, source_name, missing_reviewers = resolve_review_inputs(
        root, output_root, args.run, args.input, args.glob
    )
    name = slug(args.name or source_name or derive_name(prompt_path))
    request = parse_reviewer(args.summarizer)
    probe, attempts = resolve_and_probe(request, args.preflight_timeout)
    for attempt in attempts:
        state = "ok" if attempt.ok else "failed"
        print(f"preflight {attempt.requested}: {state} — {attempt.message}", flush=True)
    if not probe.ok or probe.reviewer is None:
        raise ReviewError(f"Summarizer failed preflight: {probe.message}")

    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    current_run = output_root / "runs" / run_id(f"{name}-summary")
    current_run.mkdir(parents=True, exist_ok=False)
    atomic_write(current_run / "prompt.md", prompt_text)
    print(
        f"started synthesis {probe.reviewer.company}/{probe.reviewer.transport}:"
        f"{probe.reviewer.model} (timeout {args.timeout}s)",
        flush=True,
    )
    result = execute_agent(
        probe.reviewer,
        root,
        prompt_text,
        args.timeout,
        summary_inputs=inputs,
        summary=True,
    )
    state = "ok" if result.ok else f"failed — {result.error}"
    print(
        f"completed synthesis {probe.reviewer.company}/{probe.reviewer.transport}: {state} "
        f"({result.duration_seconds:.1f}s)",
        flush=True,
    )
    publish_summary_artifacts(
        root,
        output_root,
        current_run,
        name,
        prompt_path,
        prompt_text,
        inputs,
        missing_reviewers,
        probe,
        result,
        started_at,
    )
    if not result.ok:
        raise ReviewError(f"Summary failed: {result.error}")
    print(f"summary: {current_run / 'summary.md'}", flush=True)
    return 0


def codex_session_final_answer(session_file: pathlib.Path) -> str:
    final_answer = ""
    for line_number, line in enumerate(read_utf8(session_file, "Codex session").splitlines(), start=1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            raise ReviewError(
                f"Codex session contains invalid JSON on line {line_number}: {session_file}"
            ) from error
        payload = event.get("payload", {})
        if not (
            event.get("type") == "response_item"
            and payload.get("type") == "message"
            and payload.get("role") == "assistant"
            and payload.get("phase") == "final_answer"
        ):
            continue
        parts = [
            str(item.get("text", ""))
            for item in payload.get("content", [])
            if item.get("type") in {"output_text", "text"}
        ]
        candidate = "".join(parts).strip()
        if candidate:
            final_answer = candidate
    if not final_answer:
        raise ReviewError(f"No final assistant answer found in Codex session: {session_file}")
    return validate_output(final_answer, "Recovered Codex summary")


def command_recover_summary(args: argparse.Namespace) -> int:
    root = resolve_input_path(args.repo_root, pathlib.Path.cwd())
    output_root = resolve_input_path(args.output_root, root, must_exist=False)
    prompt_path = resolve_input_path(args.prompt_file, root)
    prompt_text = read_utf8(prompt_path, "Prompt")
    session_file = resolve_input_path(args.codex_session_file, root)
    inputs, source_name, missing_reviewers = resolve_review_inputs(
        root, output_root, args.run, [], []
    )
    name = slug(args.name or source_name or derive_name(prompt_path))
    requested = parse_reviewer(args.summarizer)
    if requested[0] != "codex":
        raise ReviewError("Only persisted Codex sessions can currently be recovered.")
    adapter = ADAPTERS["codex"]
    reviewer = Reviewer(
        "codex",
        adapter.company,
        requested[1] or adapter.default_model,
        "codex",
    )
    output = codex_session_final_answer(session_file)
    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    current_run = output_root / "runs" / run_id(f"{name}-summary-recovered")
    current_run.mkdir(parents=True, exist_ok=False)
    atomic_write(current_run / "prompt.md", prompt_text)
    probe = ProbeResult(
        args.summarizer,
        True,
        reviewer,
        "Recovered from an already-completed persisted Codex session; no provider call was made.",
    )
    result = AgentResult(reviewer, True, output=output)
    publish_summary_artifacts(
        root,
        output_root,
        current_run,
        name,
        prompt_path,
        prompt_text,
        inputs,
        missing_reviewers,
        probe,
        result,
        started_at,
        {"recovered_from_session": str(session_file)},
    )
    print(f"summary: {current_run / 'summary.md'}", flush=True)
    return 0


def command_materialize(args: argparse.Namespace) -> int:
    root = resolve_input_path(args.repo_root, pathlib.Path.cwd())
    summary_path = resolve_input_path(args.summary_file, root)
    summary = read_utf8(summary_path, "Summary")
    normalized = normalize_kanban_summary(root, summary)
    work_items = materialize_kanban_cards(root, normalized)
    if normalized != summary:
        atomic_write(summary_path, normalized)
    for item in work_items:
        print(f"created work item {item['path']}", flush=True)
    print(f"created {len(work_items)} Kanban work item(s)", flush=True)
    return 0


def command_preflight(args: argparse.Namespace) -> int:
    requests = requested_panel(args.reviewer, args.all or not args.reviewer)
    failed = False
    successful_companies: set[str] = set()
    for request in requests:
        probe, attempts = resolve_and_probe(request, args.preflight_timeout)
        for attempt in attempts:
            state = "PASS" if attempt.ok else "FAIL"
            print(f"{state:4} {attempt.requested:8} {attempt.version or '-'} — {attempt.message}")
        if probe.ok and probe.reviewer is not None:
            successful_companies.add(probe.reviewer.company)
            if probe.reviewer.fallback_reason:
                print(f"     google selected {probe.reviewer.transport}: {probe.reviewer.fallback_reason}")
        else:
            failed = True
    if (args.all or not args.reviewer) and len(successful_companies) < 3:
        failed = True
        print("FAIL fewer than three AI companies are ready")
    return 1 if failed else 0


def add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--repo-root", default=str(repo_root_from_script()))
    parser.add_argument("--output-root", default="plans/reviews")
    parser.add_argument("--timeout", type=int, default=DEFAULT_REVIEW_TIMEOUT)
    parser.add_argument("--preflight-timeout", type=int, default=DEFAULT_PREFLIGHT_TIMEOUT)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run isolated multi-company AI reviews for Draxul.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    review = subparsers.add_parser("review", help="Run independent reviews in parallel.")
    add_common(review)
    review.add_argument("--prompt-file", required=True)
    review.add_argument("--reviewer", action="append", default=[])
    review.add_argument("--all", action="store_true")
    review.add_argument("--name")
    review.set_defaults(handler=command_review)

    summarize = subparsers.add_parser("summarize", aliases=["summarise"], help="Synthesize selected reviews.")
    add_common(summarize)
    summarize.add_argument("--prompt-file", required=True)
    inputs = summarize.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--run")
    inputs.add_argument("--input", action="append", default=[])
    inputs.add_argument("--glob", action="append", default=[])
    summarize.add_argument("--summarizer", default="codex:gpt-5.6-sol")
    summarize.add_argument("--name")
    summarize.set_defaults(handler=command_summarize)

    recover = subparsers.add_parser(
        "recover-summary",
        help="Publish a completed synthesis from a persisted Codex session without rerunning it.",
    )
    add_common(recover)
    recover.add_argument("--prompt-file", required=True)
    recover.add_argument("--run", required=True)
    recover.add_argument("--codex-session-file", required=True)
    recover.add_argument("--summarizer", default="codex:gpt-5.6-sol")
    recover.add_argument("--name")
    recover.set_defaults(handler=command_recover_summary)

    preflight = subparsers.add_parser("preflight", help="Verify reviewer connectivity.")
    add_common(preflight)
    preflight.add_argument("--reviewer", action="append", default=[])
    preflight.add_argument("--all", action="store_true")
    preflight.set_defaults(handler=command_preflight)

    materialize = subparsers.add_parser(
        "materialize", help="Create validated Kanban cards embedded in a consensus summary."
    )
    materialize.add_argument("--repo-root", default=str(repo_root_from_script()))
    materialize.add_argument("--summary-file", required=True)
    materialize.set_defaults(handler=command_materialize)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if getattr(args, "timeout", 1) <= 0 or getattr(args, "preflight_timeout", 1) <= 0:
        parser.error("timeouts must be positive")
    try:
        return int(args.handler(args))
    except ReviewError as error:
        print(f"draxul-review: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
