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
if key == "music-metadata-studio":
    core = (root / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    core_lines = core.splitlines()
    steps = [i for i, line in enumerate(core_lines)
             if line.strip() == "- name: Upload Windows integration artifact"]
    if len(steps) != 1 or core.count("uses: actions/upload-artifact@") != 1:
        errors.append("core integration artifact upload changed")
    elif not any(line.strip().startswith("if: false")
                 for line in core_lines[steps[0]+1:steps[0]+6]):
        errors.append("native core CI upload is not disabled")

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


# Shared scanner is vendored verbatim from the source SHA/blobs recorded in
# SHARED_PUBLICATION_GUARD.pin.json. Preserve product-specific checks above.
from _shared_publication_guard import scan_workflows as _scan_publication_surface

_D_EXPECTED = {"ci.yml": 1, "foobar.yml": 2}
_D_DIR = Path(__file__).resolve().parents[1] / ".github/workflows"
_D_FILES = {p.name: p.read_text(encoding="utf-8") for p in _D_DIR.iterdir()
            if p.suffix.lower() in (".yml", ".yaml")}
_D_ERRORS = _scan_publication_surface(
    _D_FILES, _D_EXPECTED,
    required_disabled_steps={
        "ci.yml": ["Stage Windows integration artifact", "Upload Windows integration artifact"],
        "foobar.yml": ["Upload DLL", "Upload combined component"],
    },
    required_disabled_jobs={"foobar.yml": ["package"]},
)
if _D_ERRORS:
    sys.exit("FAIL repository-wide publication audit:\n- " + "\n- ".join(_D_ERRORS))
_D_MUTATION = dict(_D_FILES)
_D_MUTATION["test-unreviewed-upload.yml"] = (
    "jobs:\n  leak:\n    runs-on: ubuntu-latest\n    steps:\n"
    "      - uses: actions/upload-artifact@v7\n"
)
if not _scan_publication_surface(_D_MUTATION, _D_EXPECTED):
    sys.exit("FAIL: unreviewed upload bypassed common audit")
print("PASS: source-pinned shared publication audit, negative mutation and product hold")
