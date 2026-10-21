#requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Precision CUE native SDK pin is shared with the owner-controlled, public foobar component
# template. Its Git tree proves the staged SDK *source*, not the original
# vendor .7z archive SHA256. No SDK request to foobar2000.org is necessary.
$SdkVersion = '2026-10-01'
$TemplateCommit = '31a35947a7527826c2199491692802e3ce3d8bd7'
$SdkTree = '2ba8caeb73f72633ad7f60ee5f72ed8074a0eb17'
$ExpectedArchiveHash = 'd4c55077336fae81bf8df0259b5b2748fa45ea84132c656ead93eb123cbcdc26'
$ExpectedArchiveSize = 793738L
$ExpectedArchiveUrl = 'https://www.foobar2000.org/downloads/SDK-2026-10-01.7z'
$Root = Split-Path -Parent $PSScriptRoot
$External = Join-Path $Root 'external'
$SdkRoot = Join-Path $External 'foobar2000-sdk'
$TemplateRoot = Join-Path $External 'sdk-template'
$PinStamp = Join-Path $SdkRoot '.owner-sdk-pin.txt'
$Git = (Get-Command 'git.exe' -ErrorAction Stop).Source

# Never trust a versionless, manually copied SDK merely because a single
# header exists. Fail closed on stale caches instead of silently rebuilding
# with the 2026-09-17 SDK.
$PinLine = "$SdkVersion|$TemplateCommit|$SdkTree"
if (Test-Path -LiteralPath $SdkRoot) {
    $actual = if (Test-Path -LiteralPath $PinStamp) {
        (Get-Content -LiteralPath $PinStamp -Raw).Trim()
    } else { '' }
    if ($actual -cne $PinLine) {
        throw "Unverified or stale SDK already staged at $SdkRoot. Manual cleanup of this disposable build dependency is required; no automatic overwrite."
    }
    foreach ($required in @('sdk-license.txt', 'sdk-readme.html', 'foobar2000/SDK/foobar2000.h',
        'foobar2000/shared/shared-Win32.lib', 'foobar2000/shared/shared-x64.lib')) {
        if (-not (Test-Path -LiteralPath (Join-Path $SdkRoot $required) -PathType Leaf)) {
            throw "Pinned SDK cache is incomplete: $required"
        }
    }
    if (-not (Get-Content -LiteralPath (Join-Path $SdkRoot 'sdk-readme.html') -Raw).Contains("foobar2000 SDK, version $SdkVersion")) {
        throw 'SDK cache readme version mismatch.'
    }
    Write-Host "FOOBAR_SDK_GIT_SOURCE_REUSED=PASS version=$SdkVersion"
    return
}

New-Item -ItemType Directory -Path $External -Force | Out-Null
if (-not (Test-Path -LiteralPath $TemplateRoot)) {
    & $Git clone --quiet --no-checkout --filter=blob:none 'https://github.com/lxsdd/foobar2000_component_template.git' $TemplateRoot
    if ($LASTEXITCODE -ne 0) { throw 'Pinned first-party SDK template download failed.' }
    & $Git -C $TemplateRoot checkout --quiet --detach $TemplateCommit
    if ($LASTEXITCODE -ne 0) { throw 'Pinned first-party SDK template revision unavailable.' }
}
$actualCommit = (& $Git -C $TemplateRoot rev-parse HEAD).Trim()
$actualTree = (& $Git -C $TemplateRoot rev-parse 'HEAD:vendor/sdk').Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -cne $TemplateCommit -or $actualTree -cne $SdkTree) {
    throw 'Pinned first-party SDK template commit/tree mismatch.'
}
$manifest = Get-Content -LiteralPath (Join-Path $TemplateRoot 'SDK-PIN.json') -Raw | ConvertFrom-Json
if ($manifest.sdk_version -cne $SdkVersion -or
    $manifest.upstream_url -cne $ExpectedArchiveUrl -or
    $manifest.upstream_archive_sha256 -cne $ExpectedArchiveHash -or
    [int64]$manifest.archive_size_bytes -ne $ExpectedArchiveSize) {
    throw 'Template SDK pin manifest does not match the independently documented official archive.'
}
$vendor = Join-Path $TemplateRoot 'vendor/sdk'
foreach ($entry in @(
    'sdk-license.txt', 'sdk-readme.html',
    'pfc/pfc.vcxproj', 'libPPUI/libPPUI.vcxproj',
    'foobar2000/SDK/foobar2000.h',
    'foobar2000/SDK/foobar2000_SDK.vcxproj',
    'foobar2000/foobar2000_component_client/foobar2000_component_client.vcxproj',
    'foobar2000/shared/shared-Win32.lib', 'foobar2000/shared/shared-x64.lib'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $vendor $entry) -PathType Leaf)) {
        throw "Pinned template SDK is missing $entry"
    }
}
if (-not (Get-Content -LiteralPath (Join-Path $vendor 'sdk-readme.html') -Raw).Contains("foobar2000 SDK, version $SdkVersion")) {
    throw 'Pinned SDK vendor readme version mismatch.'
}
$notice = Get-Content -LiteralPath (Join-Path $vendor 'sdk-license.txt') -Raw
if (-not $notice.Contains('Redistribution and use in source and binary forms')) {
    throw 'Pinned SDK redistributable source notice is missing.'
}

$stage = Join-Path $External ('foobar-sdk-pending-'+[guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item -Path (Join-Path $vendor '*') -Destination $stage -Recurse -Force
    [IO.File]::WriteAllText((Join-Path $stage '.owner-sdk-pin.txt'), $PinLine + [Environment]::NewLine)
    if (Test-Path -LiteralPath $SdkRoot) { throw 'SDK target appeared during immutable staging.' }
    Move-Item -LiteralPath $stage -Destination $SdkRoot -ErrorAction Stop
    Write-Host "FOOBAR_SDK_GIT_SOURCE_VERIFIED=PASS version=$SdkVersion commit=$TemplateCommit tree=$SdkTree"
    Write-Host 'FOOBAR_SDK_ORIGINAL_7Z_ARCHIVE_SHA256=NOT_TESTED (verified Git tree instead)'
} finally {
    Remove-Item -LiteralPath $stage -Force -Recurse -ErrorAction SilentlyContinue
}
