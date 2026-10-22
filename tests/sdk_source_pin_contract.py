"""Offline contract for the single shared foobar SDK Git-tree staging algorithm.

No network, SDK binaries or user files. This prevents a regression back to
vendor 7z HTTP or silently accepting a stale cache.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "scripts" / "bootstrap-sdk.ps1"
LOCAL = ROOT / "scripts" / "local-acceptance-build.ps1"
VERSION = "2026-10-01"
TEMPLATE = "31a35947a7527826c2199491692802e3ce3d8bd7"
TREE = "2ba8caeb73f72633ad7f60ee5f72ed8074a0eb17"
ARCHIVE_HASH = "d4c55077336fae81bf8df0259b5b2748fa45ea84132c656ead93eb123cbcdc26"


def violations(script: str, local: str) -> list[str]:
    failures = []
    literals = [
        f"$SdkVersion = '{VERSION}'",
        f"$TemplateCommit = '{TEMPLATE}'",
        f"$SdkTree = '{TREE}'",
        f"$ExpectedArchiveHash = '{ARCHIVE_HASH}'",
        "$ExpectedArchiveSize = 793738L",
        "rev-parse 'HEAD:vendor/sdk'",
        "'https://github.com/lxsdd/foobar2000_component_template.git'",
        "throw 'Pinned first-party SDK template commit/tree mismatch.'",
        'throw "Unverified or stale SDK already staged',
        'throw "Pinned template SDK is missing',
        "throw 'Pinned SDK redistributable source notice is missing.'",
        "FOOBAR_SDK_GIT_SOURCE_VERIFIED=PASS",
        "FOOBAR_SDK_ORIGINAL_7Z_ARCHIVE_SHA256=NOT_TESTED",
        "throw 'SDK target appeared during immutable staging.'",
    ]
    for literal in literals:
        if literal not in script:
            failures.append("missing pin/guard: " + literal)
    if "Invoke-WebRequest" in script or "Start-BitsTransfer" in script:
        failures.append("direct vendor/archive HTTP is prohibited in SDK bootstrap")
    if "SDK-$SdkVersion.7z" in script or "7z.exe" in script:
        failures.append("SDK bootstrap depends on old manufacturer 7z transport")
    if script.lstrip().startswith("#requires -Version 7.2"):
        failures.append("broke standalone Windows PowerShell 5.1 acceptance support")
    if "Get-Command git.exe" not in local:
        failures.append("acceptance preflight no longer requires Git")
    if "Get-Command 7z.exe" in local:
        failures.append("acceptance preflight incorrectly still requires 7-Zip")
    return failures


def main() -> None:
    boot = BOOT.read_text(encoding="utf-8")
    local = LOCAL.read_text(encoding="utf-8")
    assert violations(boot, local) == [], violations(boot, local)
    for broken in (
        boot.replace(TREE, "0" * 40, 1),
        boot.replace("rev-parse 'HEAD:vendor/sdk'", "rev-parse HEAD", 1),
        boot.replace("FOOBAR_SDK_ORIGINAL_7Z_ARCHIVE_SHA256=NOT_TESTED",
                     "FOOBAR_SDK_ORIGINAL_7Z_ARCHIVE_SHA256=PASS", 1),
        boot + "\nInvoke-WebRequest -Uri 'https://www.foobar2000.org/downloads/SDK.7z'\n",
        boot.replace('throw "Unverified or stale SDK already staged',
                     'Write-Host "stale SDK allowed'),
    ):
        assert violations(broken, local), "a synthetic bad SDK bootstrap escaped"
    assert violations(boot, local.replace("Get-Command git.exe", "Get-Command 7z.exe"))
    print("PASS: pinned Git-tree foobar SDK source, offline provenance and negative mutations")


if __name__ == "__main__":
    main()
