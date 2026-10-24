#!/usr/bin/env python3
"""Guard Music Metadata Studio's stable user-approved requirements IDs.

The lock contains IDs only. Detailed requirements remain in the register.
New IDs require an explicit paired change; historical IDs must not vanish.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
REGISTER = ROOT / "docs/REQUIREMENTS-REGISTER.md"
LOCK = ROOT / "docs/REQUIREMENTS-IDS.lock"
AREAS = {"WF", "UI", "CFG", "NRM", "SRC", "CUE", "FILE", "OPS"}
STATES = {"OPEN", "PARTIAL", "CI", "HOST", "BLOCKED"}
ENTRY = re.compile(
    r"^\| (MMS-([A-Z]+)-\d{2}) \| ([A-Z]+) \| (.+) \| (.+) \|$",
    re.MULTILINE,
)

def main() -> int:
    text = REGISTER.read_text(encoding="utf-8")
    found = list(ENTRY.finditer(text))
    ids = [match.group(1) for match in found]
    locked = [line.strip() for line in LOCK.read_text(encoding="utf-8").splitlines()
              if line.strip() and not line.startswith("#")]
    problems = []
    if len(ids) != len(set(ids)):
        problems.append("Duplicate requirement ID in register")
    if len(locked) != len(set(locked)):
        problems.append("Duplicate ID in lock")
    missing = sorted(set(locked) - set(ids))
    new = sorted(set(ids) - set(locked))
    if missing:
        problems.append("Previously approved requirements removed: " + ", ".join(missing))
    if new:
        problems.append("New requirements need lock update: " + ", ".join(new))
    if len(ids) < 69:
        problems.append("Unexpected reduction in the baseline 69 requirements")
    for match in found:
        if match.group(2) not in AREAS:
            problems.append("Unknown area in " + match.group(1))
        if match.group(3) not in STATES:
            problems.append("Unknown status in " + match.group(1))
    all_mentions = set(re.findall(r"\bMMS-[A-Z]+-\d{2}\b", text))
    if all_mentions - set(ids):
        problems.append("IDs referenced but not defined: " +
                        ", ".join(sorted(all_mentions - set(ids))))
    if problems:
        for problem in problems:
            print("FAIL: " + problem, file=sys.stderr)
        return 1
    print(f"PASS: {len(ids)} stable requirements across {len(AREAS)} areas")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
