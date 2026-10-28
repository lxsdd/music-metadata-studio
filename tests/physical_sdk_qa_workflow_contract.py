#!/usr/bin/env python3
"""No-network negative checks for the disposable real-foobar SDK runtime gate.

A same-repo Studio pull_request must be eligible; a fork, arbitrary PR,
release or unrelated branch cannot run the QA-only writing component.
Even on an eligible event, the component may only touch CI-generated
RUNNER_TEMP fixtures, never owner profiles/music or published artifacts.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = (ROOT / ".github/workflows/foobar.yml").read_text(encoding="utf-8")
SCRIPT = (ROOT / "scripts/qa-physical-writer-host.ps1").read_text(encoding="utf-8")
TITLE_QA = (ROOT / "scripts/qa-native-title-prefill.ps1").read_text(encoding="utf-8")
START = "  physical-sdk-runtime-qa:\n"
END = "  discogs-authorized-native:\n"

def violations(config: str, script: str) -> list[str]:
    issues: list[str] = []
    if config.count(START) != 1 or config.count(END) != 1:
        return ["physical QA job missing/ambiguous"]
    qa = config.split(START, 1)[1].split(END, 1)[0]
    requirements = [
        "github.event_name == 'pull_request'",
        "github.event.pull_request.head.repo.full_name == github.repository",
        "github.event.pull_request.head.ref == 'feature/online-metadata-discovery-foundation-20261009'",
        "github.event.pull_request.base.ref == 'main'",
        "github.event_name == 'push'",
        "github.ref == 'refs/heads/feature/online-metadata-discovery-foundation-20261009'",
        "runs-on: windows-2022",
        "platform: [Win32, x64]",
        "needs: static-audit",
        "permissions:\n      contents: read",
        "./scripts/qa-physical-writer-host.ps1",
        "if: always()",
        "Remove-Item -LiteralPath 'out/physical-write-qa','build/physical-write-qa'",
    ]
    for item in requirements:
        if item not in qa:
            issues.append("missing restricted event/runner/cleanup gate: " + item)
    if "environment: discogs-qa" in qa or "MMS_DISCOGS_QA_TOKEN" in qa:
        issues.append("real physical QA must not consume protected Discogs secrets")
    if "uses: actions/upload-artifact@" in qa or "uses: actions/create-release@" in qa:
        issues.append("QA-only writer must not publish a binary")
    if "MMS_QA_WRITE_SOURCE" not in script or "MMS_QA_CUE_SOURCE" not in script:
        issues.append("actual fixture-specific native-source scoping missing")
    for item in (
        "$env:GITHUB_ACTIONS -cne 'true'",
        "Test-Path -LiteralPath $env:RUNNER_TEMP -PathType Container",
        "('mms-qa-write-'+[guid]::NewGuid().ToString('N'))",
        "Join-Path $env:RUNNER_TEMP",
        "Get-Process foobar2000",
        "MMS_NATIVE_SDK_PHYSICAL_NOOP=PASS",
        "MMS_NATIVE_SDK_POSTWRITE_NOOP=PASS",
        "MMS_NATIVE_EXTERNAL_CUE_POSTWRITE_NOOP=PASS",
        "MMS_NATIVE_EXTERNAL_CUE_HOST_QA_",
        "Remove-Item -LiteralPath $work -Force -Recurse",
    ):
        if item not in script:
            issues.append("synthetic isolated writer host evidence missing: " + item)
    if "qa-native-title-prefill.ps1" not in script:
        issues.append("real GUI original-tag prefill regression was not executed")
    for marker in (
        "GITHUB_ACTIONS -cne 'true'",
        "mms-writer-qa.flac",
        "[IO.Path]::GetFullPath($Source)",
        "StartsWith($root+",
        "MmsSyntheticTitleHost",
        "SendText(edit,0x000D",
        "GetWindowThreadProcessId",
        "EditText($dialog,291)",
        "$actual -ceq '1000 Years'",
        "MMS_NATIVE_ORIGINAL_TITLE_PREFILL_",
        "ReadAllBytes($physical)",
        "LastWriteTimeUtc",
    ):
        if marker not in TITLE_QA:
            issues.append("missing native GUI synthetic-only protected title check: " + marker)
    if "GetWindowTextW(edit" in TITLE_QA:
        issues.append("cross-process EDIT cannot be verified via GetWindowTextW")
    return issues

def main() -> None:
    global TITLE_QA
    assert not violations(CONFIG, SCRIPT), violations(CONFIG, SCRIPT)
    # Mutations MUST target the QA job, not the identically named
    # Win32/x64 build matrix belonging to another job.
    def mutate_qa(needle: str, replacement: str) -> str:
        prefix, rest = CONFIG.split(START, 1)
        qa, tail = rest.split(END, 1)
        assert qa.count(needle) == 1, "ambiguous QA mutation target"
        return prefix + START + qa.replace(needle, replacement, 1) + END + tail

    for altered in (
        mutate_qa(
            "github.event.pull_request.head.repo.full_name == github.repository",
            "github.event.pull_request.head.repo.full_name != github.repository"),
        mutate_qa("github.event.pull_request.base.ref == 'main'",
                  "github.event.pull_request.base.ref == 'develop'"),
        mutate_qa("platform: [Win32, x64]", "platform: [x64]"),
        mutate_qa("./scripts/qa-physical-writer-host.ps1",
                  "./scripts/fake-writer-qa.ps1"),
        mutate_qa("needs: static-audit", "needs: []"),
    ):
        assert violations(altered, SCRIPT), "negative workflow mutation escaped"
    for mutated in (
        SCRIPT.replace("$env:GITHUB_ACTIONS -cne 'true'",
                       "$env:GITHUB_ACTIONS -ceq 'true'", 1),
        SCRIPT.replace("MMS_QA_CUE_SOURCE", "MMS_EXTERNAL_CUE_PATH"),
        SCRIPT.replace("MMS_NATIVE_SDK_POSTWRITE_NOOP=PASS", "QA_UNCHECKED"),
        SCRIPT.replace("qa-native-title-prefill.ps1", "no-native-qa.ps1"),
    ):
        assert violations(CONFIG, mutated), "unsafe host script mutation escaped"
    for altered in (
        TITLE_QA.replace("SendText(edit,0x000D",
                         "GetWindowTextW(edit,0x000D"),
        TITLE_QA.replace("GetWindowThreadProcessId",
                         "GetUntrustedProcessId"),
        TITLE_QA.replace("$actual -ceq '1000 Years'",
                         "$actual -cne '1000 Years'"),
        TITLE_QA.replace("mms-writer-qa.flac", "user-library.flac"),
        TITLE_QA.replace("LastWriteTimeUtc", "CreationTimeUtc"),
    ):
        old = TITLE_QA
        TITLE_QA = altered
        try:
            assert violations(CONFIG, SCRIPT), "negative GUI QA mutation escaped"
        finally:
            TITLE_QA = old
    print("PASS: trusted synthetic real SDK physical/CUE + native cross-process original TITLE QA, no public artifacts")

if __name__ == "__main__":
    main()
