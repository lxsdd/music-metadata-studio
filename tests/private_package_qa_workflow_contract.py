#!/usr/bin/env python3
"""Offline safeguards for ephemeral, SHA-pinned read-only Studio package QA."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
YAML=(ROOT / ".github/workflows/foobar.yml").read_text(encoding="utf-8")
START="  private-package-qualification:\n"
END="  # Internal PRs may publish short-lived QA artifacts"

def failures(content: str) -> list[str]:
    if content.count(START)!=1 or content.count(END)!=1:
        return ["missing unambiguous private packaging job"]
    block=content.split(START,1)[1].split(END,1)[0]
    required=[
        "github.event_name == 'pull_request'",
        "github.event.pull_request.head.repo.full_name == github.repository",
        "github.event.pull_request.head.ref == 'feature/online-metadata-discovery-foundation-20261009'",
        "github.event.pull_request.base.ref == 'main'",
        "github.event_name == 'push'",
        "contains(github.event.head_commit.message, '[qualify-private-package]')",
        "needs: [static-audit, windows-build, physical-sdk-runtime-qa]",
        "permissions:\n      contents: read",
        "runs-on: windows-2022",
        "github.event.pull_request.head.sha || github.sha",
        "Join-Path $env:RUNNER_TEMP 'mms-private-qualify'",
        "local-acceptance-build.ps1 -SourceSha $sha -OutputDirectory $target",
        "BUILD-MANIFEST.txt",
        "commit=' + $sha",
        "status=PASS",
        "Remove-Item -LiteralPath $target -Recurse -Force",
    ]
    out=["missing protected ephemeral SHA condition: "+x for x in required if x not in block]
    forbidden=("uses: actions/upload-artifact@", "secrets.MMS_DISCOGS_QA_TOKEN",
               "environment: discogs-qa", "-EnablePhysicalWrites",
               "uses: actions/create-release@")
    out.extend("forbidden package exposure: "+x for x in forbidden if x in block)
    return out

def tamper(needle: str, replacement: str) -> str:
    prefix, remainder=YAML.split(START,1)
    block, suffix=remainder.split(END,1)
    assert block.count(needle)==1, "ambiguous package mutation"
    return prefix+START+block.replace(needle,replacement,1)+END+suffix

def main() -> None:
    assert not failures(YAML), failures(YAML)
    for changed in (
        tamper("github.event.pull_request.head.repo.full_name == github.repository",
               "github.event.pull_request.head.repo.full_name != github.repository"),
        tamper("github.event.pull_request.base.ref == 'main'",
               "github.event.pull_request.base.ref == 'fork'"),
        tamper("github.event.pull_request.head.sha || github.sha",
               "github.sha"),
        tamper("needs: [static-audit, windows-build, physical-sdk-runtime-qa]",
               "needs: [static-audit]"),
        tamper("Join-Path $env:RUNNER_TEMP 'mms-private-qualify'",
               "Join-Path $env:USERPROFILE 'Music'"),
        tamper("Remove-Item -LiteralPath $target -Recurse -Force",
               "Write-Host 'leaving temporary package in runner'"),
    ):
        assert failures(changed), "unsafe internal package workflow mutation escaped"
    print("PASS: trusted PR exact-source ephemeral package, SDK QA dependency, fork exclusion, no publish, no writer activation")

if __name__=="__main__":
    main()
