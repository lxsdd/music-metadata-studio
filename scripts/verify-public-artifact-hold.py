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


# Repository-wide second gate: inspect every Actions workflow, including newly
# added files, rather than only previously known upload step names.
def _scan_publication_surface(_files, _expected):
    import re as _re
    _issues, _counts = [], {}

    def _indent(_s):
        return len(_s) - len(_s.lstrip(" "))

    def _is_disabled(_s, _col):
        return (_indent(_s) == _col and
                _re.match(r"^\s*if:\s*false(?:\s*(?:#.*)?)?$", _s) is not None)

    def _job_disabled(_lines, _i):
        _jobs = [_j for _j in range(_i + 1)
                 if _re.match(r"^  [A-Za-z0-9_-]+:\s*(?:#.*)?$", _lines[_j])]
        return (bool(_jobs) and any(_is_disabled(_s, 4)
                for _s in _lines[_jobs[-1]+1:_i+1]))

    for _name, _text in _files.items():
        _lines = _text.splitlines()
        for _i, _line in enumerate(_lines):
            _s = _line.strip()
            if _s.startswith("#"):
                continue
            if _re.match(r"^-?\s*uses:\s*actions/upload-artifact@", _s, _re.I):
                _counts[_name] = _counts.get(_name, 0) + 1
                _level = _indent(_line) if _s.startswith("- uses:") else _indent(_line) - 2
                _starts = [_j for _j in range(_i + 1)
                           if _indent(_lines[_j]) == _level
                           and _lines[_j].lstrip().startswith("- ")]
                if not _starts:
                    _issues.append(f"{_name}:{_i+1}: unidentified upload step")
                    continue
                _start = _starts[-1]
                _end = next((_k for _k in range(_start + 1, len(_lines))
                             if _lines[_k].strip()
                             and not _lines[_k].lstrip().startswith("#")
                             and _indent(_lines[_k]) <= _level), len(_lines))
                if (not any(_is_disabled(_v, _level + 2)
                            for _v in _lines[_start+1:_end])
                        and not _job_disabled(_lines, _i)):
                    _issues.append(f"{_name}:{_i+1}: ungated upload")
            if (_re.search(r"\bgh\s+release\s+create\b", _s)
                or _re.match(r"^-?\s*uses:\s*(?:softprops/action-gh-release|actions/create-release)@", _s, _re.I)):
                if not _job_disabled(_lines, _i):
                    _issues.append(f"{_name}:{_i+1}: ungated release")
    for _name, _count in _expected.items():
        if _counts.get(_name, 0) != _count:
            _issues.append(f"{_name}: expected {_count} uploads, found {_counts.get(_name, 0)}")
    for _name in _counts:
        if _name not in _expected:
            _issues.append(f"{_name}: unreviewed new upload workflow")
    return _issues

_D_EXPECTED = {"ci.yml":1,"foobar.yml":2}
_D_DIR = Path(__file__).resolve().parents[1] / ".github/workflows"
_D_FILES = {p.name: p.read_text(encoding="utf-8") for p in _D_DIR.iterdir()
            if p.suffix.lower() in (".yml", ".yaml")}
if not _D_FILES:
    sys.exit("FAIL: no workflow files to audit")
_D_ERRORS = _scan_publication_surface(_D_FILES, _D_EXPECTED)
if _D_ERRORS:
    sys.exit("FAIL repository-wide binary publication audit:\n- " + "\n- ".join(_D_ERRORS))
_D_MUTATION = dict(_D_FILES)
_D_MUTATION["test-unreviewed-upload.yml"] = (
    "jobs:\n  leak:\n    runs-on: ubuntu-latest\n    steps:\n"
    "      - uses: actions/upload-artifact@v7\n")
if not _scan_publication_surface(_D_MUTATION, _D_EXPECTED):
    sys.exit("FAIL: an unreviewed binary upload bypassed the audit")
print(f"PASS: {_D_DIR} workflow audit including new-workflow bypass regression")
