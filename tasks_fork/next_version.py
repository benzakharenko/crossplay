#!/usr/bin/env python3
"""Print the version the next release of this fork should carry, and optionally stamp it.

The device's updater compares only three numbers (major.minor.patch) and ignores
any suffix, so "1.16.1-tasks.2" would never look newer than "1.16.1". Instead the
fork folds its own build count into the patch number:

    CrossPlay 1.16.1  ->  1.16.201, 1.16.202, ...   ((patch + 1) * 100 + n)
    CrossPlay 1.16.2  ->  1.16.301, ...
    CrossPlay 1.17.0  ->  1.17.101, ...

The +1 keeps fork tags clear of CrossPlay's own (1.17.1, 1.17.2 ...), which a
fork inherits.

Every step is larger than the last, so the device always sees the newest build
as an update. Allows 99 fork builds per CrossPlay release.

Usage: next_version.py            print e.g. 1.16.203
       next_version.py --stamp V  write V into platformio.ini's [crossplay] version
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INI = os.path.join(ROOT, "platformio.ini")
SECTION = re.compile(r"(\[crossplay\]\s*\nversion\s*=\s*)(\d+)\.(\d+)\.(\d+)")


def upstream_version():
    with open(INI, encoding="utf-8") as f:
        m = SECTION.search(f.read())
    if not m:
        raise SystemExit("platformio.ini: [crossplay] version not found")
    return int(m.group(2)), int(m.group(3)), int(m.group(4))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stamp")
    args = ap.parse_args()

    if args.stamp:
        with open(INI, encoding="utf-8") as f:
            text = f.read()
        major, minor, patch = args.stamp.split(".")
        text, n = SECTION.subn(rf"\g<1>{major}.{minor}.{patch}", text, count=1)
        if n != 1:
            raise SystemExit("could not stamp version")
        with open(INI, "w", encoding="utf-8") as f:
            f.write(text)
        return 0

    major, minor, patch = upstream_version()
    if patch > 99:
        raise SystemExit("platformio.ini already holds a stamped fork version; run on a clean checkout")
    base = (patch + 1) * 100
    tags = subprocess.run(["git", "tag", "--list", f"v{major}.{minor}.*"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout.split()
    used = [int(t.split(".")[2]) - base for t in tags
            if re.fullmatch(rf"v{major}\.{minor}\.\d+", t) and base < int(t.split(".")[2]) < base + 100]
    n = max(used, default=0) + 1
    if n > 99:
        raise SystemExit("99 fork builds on this CrossPlay release; widen the scheme")
    print(f"{major}.{minor}.{base + n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
