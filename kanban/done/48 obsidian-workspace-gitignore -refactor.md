# 48 Obsidian Workspace Gitignore

## Why This Exists

`plans/.obsidian/workspace.json` is committed to the repository. This file records Obsidian's open tabs, active pane layout, and recently viewed files. It changes on every editing session, generating constant diff noise for any agent or collaborator not using Obsidian.

Identified by: **Claude** (smells #12).

## Goal

Add `plans/.obsidian/workspace.json` (and similar ephemeral Obsidian state files) to `.gitignore`. Remove the file from tracking.

## Implementation Plan

- [x] Read the root `.gitignore` to check existing Obsidian entries.
- [x] Add the following to `.gitignore`:
  ```
  plans/.obsidian/workspace.json
  plans/.obsidian/workspace-mobile.json
  plans/.obsidian/cache
  ```
- [x] Run `git rm --cached plans/.obsidian/workspace.json` to untrack the file without deleting it locally.
- [x] Verify `git status` no longer shows the file as modified after a simulated Obsidian edit.
- [x] Commit the `.gitignore` change.

## Sub-Agent Split

Single agent. Trivial change.
