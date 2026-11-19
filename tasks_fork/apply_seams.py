#!/usr/bin/env python3
"""Re-apply this fork's edits to CrossPlay-owned files. Safe to run any number of times.

This fork (CrossPlay + a Notion Tasks app) changes only a few files that CrossPlay
also owns. When a merge from CrossPlay conflicts in one of them, the sync workflow
takes CrossPlay's version of the file and runs this script to put our edit back,
so a conflict in a seam never needs a human.

Seams:
  1. src/network/ReleaseSources.h -- the device's "Check for updates" must ask THIS
     repo's releases. Left pointing at CrossPlay, an update would flash plain
     CrossPlay over the fork and the Tasks app would vanish.
  2. src/apps_local/Shelf.cpp -- one #include and one row at the end of the Apps
     list. Only applied once src/apps_local/tasks/TasksActivity.h exists.

Usage: apply_seams.py [--repo OWNER/NAME] [--check]
  --check  exit 1 if any seam is missing, change nothing.
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEAM_FILES = ["src/network/ReleaseSources.h", "src/apps_local/Shelf.cpp"]

SHELF_INCLUDE = '#include "tasks/TasksActivity.h"  // tasks-fork seam'
SHELF_ROW = '    {"TASKS", &icon_check_32, &TasksActivity::create},  // tasks-fork seam'


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def write(rel, text):
    with open(os.path.join(ROOT, rel), "w", encoding="utf-8") as f:
        f.write(text)


def release_sources(text, repo):
    url = f"https://api.github.com/repos/{repo}/releases/latest"
    block = re.search(r"(constexpr const char\* const kUrls\[\] = \{)(.*?)(\};)", text, re.S)
    if not block:
        raise SystemExit("ReleaseSources.h: kUrls table not found; CrossPlay changed its shape, update this script")
    body = f'\n    // tasks-fork seam: only this fork\'s releases.\n    "{url}",\n'
    return text[: block.start(2)] + body + text[block.end(2):]


def shelf(text):
    if "tasks-fork seam" in text:
        return text
    includes = list(re.finditer(r'^#include "[a-z_]+/[A-Za-z]+Activity\.h"\n', text, re.M))
    if not includes:
        raise SystemExit("Shelf.cpp: app #include block not found; update this script")
    at = includes[-1].end()
    text = text[:at] + SHELF_INCLUDE + "\n" + text[at:]
    apps = re.search(r"constexpr shelf::Item kApps\[\] = \{.*?\n\};", text, re.S)
    if not apps:
        raise SystemExit("Shelf.cpp: kApps table not found; update this script")
    close = apps.end() - 2  # position of the closing "};"
    return text[:close] + SHELF_ROW + "\n" + text[close:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "benzakharenko/crossplay"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    plans = {"src/network/ReleaseSources.h": lambda t: release_sources(t, args.repo)}
    if os.path.exists(os.path.join(ROOT, "src/apps_local/tasks/TasksActivity.h")):
        plans["src/apps_local/Shelf.cpp"] = shelf

    missing = []
    for rel, fix in plans.items():
        before = read(rel)
        after = fix(before)
        if after != before:
            missing.append(rel)
            if not args.check:
                write(rel, after)
    if args.check and missing:
        print("seams missing in: " + ", ".join(missing))
        return 1
    print(("re-applied: " + ", ".join(missing)) if missing else "all seams in place")
    return 0


if __name__ == "__main__":
    sys.exit(main())
