#!/usr/bin/env python3
"""Reject accidental re-enabling of public GitHub binary artifacts."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
config = {
    "music-metadata-studio": {
        "workflow": root / ".github/workflows/foobar.yml",
        "steps": ("Upload DLL", "Upload combined component"),
        "jobs": ("package",),
        "uploads": 2,
    },
    "foo_dj_library_bridge": {
        "workflow": root / ".github/workflows/build.yml",
        "steps": ("Upload DLL", "Upload qualified component candidate"),
        "jobs": ("package-component",),
        "uploads": 2,
    },
}
# Project identity is detected using the source tree, not external arguments.
key = "foo_dj_library_bridge" if (root / "foo_dj_library_bridge.sln").exists() else "music-metadata-studio"
entry = config[key]
source = entry["workflow"].read_text(encoding="utf-8")
lines = source.splitlines()
errors = []

def gated_line(label):
    found = [i for i, line in enumerate(lines) if line.strip() == label]
    if len(found) != 1:
        errors.append(f"{label!r}: expected one definition, found {len(found)}")
        return
    block = lines[found[0] + 1 : found[0] + 7]
    if not any(line.strip().startswith("if: false") for line in block):
        errors.append(f"{label!r}: no explicit 'if: false' gate")

for step in entry["steps"]:
    gated_line("- name: " + step)
for job in entry["jobs"]:
    gated_line(job + ":")
if source.count("uses: actions/upload-artifact@") != entry["uploads"]:
    errors.append("new/removed artifact upload action; manual review required")
if key == "foo_dj_library_bridge":
    release = (root / ".github/workflows/release-qualified.yml").read_text(encoding="utf-8").splitlines()
    positions = [i for i, line in enumerate(release) if line == "  release:"]
    if len(positions) != 1 or not any(
        line.strip().startswith("if: false")
        for line in release[positions[0]+1:positions[0]+6]
    ):
        errors.append("manual release job is not disabled")

if errors:
    sys.exit("FAIL: public binary distribution gate:\n- " + "\n- ".join(errors))
print("PASS: public uploads and release jobs disabled; build/tests remain enabled")
