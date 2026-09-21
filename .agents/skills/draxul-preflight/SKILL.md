---
name: draxul-preflight
description: Verify that Draxul's Codex, Claude, Antigravity/Gemini, and Grok review providers are installed, authenticated, model-accessible, and returning live responses. Use before multi-AI reviews or when reviewer connectivity, login, model, timeout, or fallback behavior is failing.
---

# Draxul reviewer preflight

Run the shared review runner from the repository root:

```text
py .agents/skills/draxul-review/scripts/review.py preflight
```

By default, check the review panel: OpenAI `gpt-6-astra` and Anthropic `claude-fable-5-1` (Claude Fable 5.1). Pass `--all` to check all configured companies, including Google and xAI. For selected providers or models, pass one or more `--reviewer transport:model` arguments instead.

Report each transport’s executable/version, authentication or model-discovery result, live nonce result, and any Google Agy-to-Gemini fallback. Default and explicit preflights require every selected reviewer to pass; `--all` also requires at least three distinct AI companies.

Do not initiate login flows automatically. Give the failed CLI’s own error and tell the user which CLI must be launched or authenticated.
