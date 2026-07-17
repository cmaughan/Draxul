#!/usr/bin/env python3
"""Sync local work-item markdown files to the GitHub Draxul project board.

kanban/pending/  -> Status: Backlog
kanban/ice-box/  -> Status: IceBox
kanban/done/     -> Status: Done

Idempotent: matches by title, never creates duplicates. Items already in
Ready/In progress/In review are left untouched (in-flight work).
"""

import json
import os
import subprocess
import sys

PROJECT_OWNER = "cmaughan"
PROJECT_NUMBER = 1
PROJECT_ID = "PVT_kwHOAAHWf84BSF08"
STATUS_FIELD_ID = "PVTSSF_lAHOAAHWf84BSF08zg_uhVc"
STATUS_BACKLOG = "f75ad846"
STATUS_ICEBOX = "1402824a"
STATUS_DONE   = "98236657"

# Statuses that indicate the item is actively in flight — never overwrite these
STATUS_PRESERVE = {"61e4505c", "47fc9ee4", "df73e18b"}  # Ready, In progress, In review

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BACKLOG_DIR = os.path.join(REPO_ROOT, "kanban", "pending")
ICEBOX_DIR  = os.path.join(REPO_ROOT, "kanban", "ice-box")
DONE_DIR    = os.path.join(REPO_ROOT, "kanban", "done")


def gh(*args, input_data=None):
    result = subprocess.run(
        ["gh"] + list(args),
        capture_output=True, text=True, input=input_data
    )
    if result.returncode != 0:
        print(f"ERROR: gh {' '.join(args)}", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        sys.exit(1)
    return result.stdout.strip()


def graphql(query, **variables):
    args = ["api", "graphql", "-f", f"query={query}"]
    for k, v in variables.items():
        args += ["-f", f"{k}={v}"]
    out = gh(*args)
    return json.loads(out)


def get_title(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# "):
                return line[2:].strip()
    # Fallback: derive from filename
    name = os.path.basename(path)
    name = name.replace(".md", "")
    # Strip trailing tag like " -feature"
    for tag in [" -feature", " -bug", " -test", " -refactor"]:
        name = name.replace(tag, "")
    return name


def get_existing_items():
    """Return dict of title -> item_id for all current project items."""
    data = graphql("""
query($proj: ID!) {
  node(id: $proj) {
    ... on ProjectV2 {
      items(first: 100) {
        nodes {
          id
          content {
            ... on DraftIssue { title }
            ... on Issue { title }
          }
          fieldValues(first: 10) {
            nodes {
              ... on ProjectV2ItemFieldSingleSelectValue {
                optionId
                field { ... on ProjectV2SingleSelectField { id } }
              }
            }
          }
        }
      }
    }
  }
}""", proj=PROJECT_ID)
    items = {}
    for node in data["data"]["node"]["items"]["nodes"]:
        content = node.get("content") or {}
        title = content.get("title", "")
        if title:
            # Also capture current status optionId
            status = None
            for fv in node["fieldValues"]["nodes"]:
                if fv and fv.get("field", {}).get("id") == STATUS_FIELD_ID:
                    status = fv.get("optionId")
            items[title] = {"id": node["id"], "status": status}
    return items


def create_draft(title):
    data = graphql("""
mutation($proj: ID!, $title: String!) {
  addProjectV2DraftIssue(input: {projectId: $proj, title: $title}) {
    projectItem { id }
  }
}""", proj=PROJECT_ID, title=title)
    return data["data"]["addProjectV2DraftIssue"]["projectItem"]["id"]


def set_status(item_id, option_id):
    graphql("""
mutation($proj: ID!, $item: ID!, $field: ID!, $opt: String!) {
  updateProjectV2ItemFieldValue(input: {
    projectId: $proj
    itemId: $item
    fieldId: $field
    value: { singleSelectOptionId: $opt }
  }) {
    projectV2Item { id }
  }
}""", proj=PROJECT_ID, item=item_id, field=STATUS_FIELD_ID, opt=option_id)


# Board docs that live alongside the cards but are not work items.
NON_ITEM_FILES = {"index.md", "readme.md"}


def collect_items(directory):
    items = []
    for fname in sorted(os.listdir(directory)):
        if fname.endswith(".md") and fname.lower() not in NON_ITEM_FILES:
            path = os.path.join(directory, fname)
            title = get_title(path)
            items.append(title)
    return items


def sync(title, desired_status, existing):
    status_name = {STATUS_BACKLOG: "Backlog", STATUS_ICEBOX: "IceBox", STATUS_DONE: "Done"}.get(desired_status, desired_status)
    if title in existing:
        item = existing[title]
        if item["status"] in STATUS_PRESERVE:
            print(f"  [skip]   {title} (active)")
        elif item["status"] == desired_status:
            print(f"  [ok]     {title}")
        else:
            print(f"  [update] {title} -> {status_name}")
            set_status(item["id"], desired_status)
    else:
        print(f"  [create] {title} -> {status_name}")
        item_id = create_draft(title)
        set_status(item_id, desired_status)


def main():
    print("Fetching existing project items...")
    existing = get_existing_items()
    print(f"  Found {len(existing)} existing items\n")

    print("Syncing Backlog items...")
    for title in collect_items(BACKLOG_DIR):
        sync(title, STATUS_BACKLOG, existing)

    print("\nSyncing IceBox items...")
    for title in collect_items(ICEBOX_DIR):
        sync(title, STATUS_ICEBOX, existing)

    print("\nSyncing Done items...")
    for title in collect_items(DONE_DIR):
        sync(title, STATUS_DONE, existing)

    print("\nDone.")


if __name__ == "__main__":
    main()
