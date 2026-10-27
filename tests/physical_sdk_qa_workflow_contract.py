#!/usr/bin/env python3
"""No-network negative checks for the disposable real-foobar SDK runtime gate.

Only canonical same-repo Studio and Studio/Precision CUE integration
pull_requests (or their exact branch pushes) are eligible; a fork,
arbitrary PR, release or unrelated branch must not run the QA writer.
Even on an eligible event, the component may only touch CI-generated
RUNNER_TEMP fixtures, never owner profiles/music or published artifacts.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = (ROOT / ".github/workflows/foobar.yml").read_text(encoding="utf-8")
SCRIPT = (ROOT / "scripts/qa-physical-writer-host.ps1").read_text(encoding="utf-8")
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
    # The full event predicate is intentionally pinned. Presence checks alone
    # would allow an accidentally appended '|| true' to bypass all restrictions.
    expected_condition = """    if: >-
      (github.event_name == 'push' &&
       github.ref == 'refs/heads/feature/online-metadata-discovery-foundation-20261009') ||
      (github.event_name == 'pull_request' &&
       github.event.pull_request.head.repo.full_name == github.repository &&
       github.event.pull_request.head.ref == 'feature/online-metadata-discovery-foundation-20261009' &&
       github.event.pull_request.base.ref == 'main') ||
      (github.event_name == 'push' &&
       github.ref == 'refs/heads/feature/studio-precision-cue-integration-20261026') ||
      (github.event_name == 'pull_request' &&
       github.event.pull_request.head.repo.full_name == github.repository &&
       github.event.pull_request.head.ref == 'feature/studio-precision-cue-integration-20261026' &&
       github.event.pull_request.base.ref == 'feature/online-metadata-discovery-foundation-20261009')"""
    actual_condition = "    if: >-\\n" + qa.split("    if: >-\\n", 1)[1].split("    needs: static-audit\\n", 1)[0] if "    if: >-\\n" in qa else ""
    if actual_condition != expected_condition + "\\n":
        issues.append("untrusted physical QA event predicate")
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
    return issues

def main() -> None:
    assert not violations(CONFIG, SCRIPT), violations(CONFIG, SCRIPT)
    # Mutations MUST target the QA job, not the identically named
    # Win32/x64 build matrix belonging to another job.
    def mutate_qa(needle: str, replacement: str) -> str:
        prefix, rest = CONFIG.split(START, 1)
        qa, tail = rest.split(END, 1)
        assert qa.count(needle) >= 1, "missing QA mutation target"
        return prefix + START + qa.replace(needle, replacement) + END + tail

    for altered in (
        mutate_qa(
            "github.event.pull_request.head.repo.full_name == github.repository",
            "github.event.pull_request.head.repo.full_name != github.repository"),
        mutate_qa("github.event.pull_request.base.ref == 'main'",
                  "github.event.pull_request.base.ref == 'develop'"),
        mutate_qa("platform: [Win32, x64]", "platform: [x64]"),
        mutate_qa("github.ref == 'refs/heads/feature/studio-precision-cue-integration-20261026'",
                  "github.ref == 'refs/heads/any-other-branch'"),
        mutate_qa("github.event.pull_request.base.ref == 'feature/online-metadata-discovery-foundation-20261009'",
                  "github.event.pull_request.base.ref == 'main'"),
        mutate_qa("github.event.pull_request.head.ref == 'feature/studio-precision-cue-integration-20261026'",
                  "github.event.pull_request.head.ref == 'feature/any-other-branch'"),
        mutate_qa("    needs: static-audit", "    needs: static-audit || true"),
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
    ):
        assert violations(CONFIG, mutated), "unsafe host script mutation escaped"
    print("PASS: real SDK physical/CUE QA allowed only trusted same-repo Studio/integration PRs, synthetic runner fixtures, zero artifacts and full no-op checks")

if __name__ == "__main__":
    main()
