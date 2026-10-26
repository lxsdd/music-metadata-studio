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
        "github.event_name == 'push'",
        "github.ref == 'refs/heads/feature/studio-precision-cue-integration-20261026'",
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



# Independently protect the integration's ephemeral real Win32/x64 package
# qualification. No new GitHub artifact upload, public DLL, secrets, writes,
# fork PR or unrelated branch can trigger this job.
INTEGRATION_START="  precision-integration-package-qa:\n"

def integration_block(content: str) -> str:
    if content.count(INTEGRATION_START)!=1:
        raise ValueError("missing or duplicate integration package job")
    return content.split(INTEGRATION_START,1)[1]

def integration_failures(content: str) -> list[str]:
    try:
        block=integration_block(content)
    except ValueError as error:
        return [str(error)]
    required=(
        "github.event_name == 'pull_request'",
        "github.event.pull_request.head.repo.full_name == github.repository",
        "github.event.pull_request.head.ref == 'feature/studio-precision-cue-integration-20261026'",
        "github.event.pull_request.base.ref == 'feature/online-metadata-discovery-foundation-20261009'",
        "needs: [static-audit, host-gate-script, windows-build]",
        "runs-on: windows-2022",
        "permissions:\n      contents: read",
        "github.event.pull_request.head.sha || github.sha",
        "Join-Path $env:RUNNER_TEMP",
        "scripts/local-acceptance-build.ps1 -SourceSha $sha -OutputDirectory $target",
        "precision_cue_create_only_enabled=true",
        "studio_existing_cue_value_writer_enabled=false",
        "external_cue_write_scope=precision-new-sibling-create-only",
        "Get-FileHash -LiteralPath $packages[0].FullName -Algorithm SHA256",
        "Remove-Item -LiteralPath $target -Recurse -Force",
        "MMS_PRECISION_PRIVATE_PACKAGE=PASS",
    )
    issues=["Missing guarded integrated package feature: "+x for x in required if x not in block]
    for forbidden in (
        "uses: actions/upload-artifact@", "uses: actions/create-release@",
        "environment: discogs-qa", "secrets.", "-EnablePhysicalWrites",
        "MMS_DISCOGS_QA_TOKEN",
    ):
        if forbidden in block:
            issues.append("Integrated package has an unsafe feature: "+forbidden)
    return issues

def tamper_integration(needle: str, replacement: str) -> str:
    prefix, block=YAML.split(INTEGRATION_START,1)
    assert block.count(needle)==1, "ambiguous integration QA mutation"
    return prefix+INTEGRATION_START+block.replace(needle,replacement,1)

def check_integration_mutations() -> None:
    assert not integration_failures(YAML), integration_failures(YAML)
    for mutated in (
        tamper_integration("github.ref == 'refs/heads/feature/studio-precision-cue-integration-20261026'",
                           "github.ref == 'refs/heads/any-branch'"),
        tamper_integration("github.event.pull_request.head.repo.full_name == github.repository",
                           "github.event.pull_request.head.repo.full_name != github.repository"),
        tamper_integration("github.event.pull_request.base.ref == 'feature/online-metadata-discovery-foundation-20261009'",
                           "github.event.pull_request.base.ref == 'main'"),
        tamper_integration("github.event.pull_request.head.ref == 'feature/studio-precision-cue-integration-20261026'",
                           "github.event.pull_request.head.ref == 'other-branch'"),
        tamper_integration("needs: [static-audit, host-gate-script, windows-build]",
                           "needs: []"),
        tamper_integration("Join-Path $env:RUNNER_TEMP",
                           "Join-Path $env:USERPROFILE"),
        tamper_integration("precision_cue_create_only_enabled=true",
                           "precision_cue_create_only_enabled=false"),
        tamper_integration("Remove-Item -LiteralPath $target -Recurse -Force",
                           "Write-Host 'leaving package'"),
    ):
        assert integration_failures(mutated), "unsafe integration package mutation escaped"

if __name__=="__main__":
    main()
    check_integration_mutations()
    print("PASS: private integrated Precision CUE package restricted to trusted worker, with truthful manifest, SHA256 and cleanup")
