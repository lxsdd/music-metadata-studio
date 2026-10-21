$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Official SDK pin qualified independently in
# lxsdd/foobar2000_component_template SDK-PIN.json.
$SdkVersion = '2026-10-01'
$ExpectedSize = 793738
$ExpectedSHA256 = 'd4c55077336fae81bf8df0259b5b2748fa45ea84132c656ead93eb123cbcdc26'
# Existing owner-controlled template contains the previously pinned SDK tree.
# Git SHA/tree pins are separate proof from the official .7z SHA256, NOT a
# pretext to accept a mismatched official archive or an arbitrary mirror.
$TemplateCommit = '31a35947a7527826c2199491692802e3ce3d8bd7'
$TemplateSdkTree = '2ba8caeb73f72633ad7f60ee5f72ed8074a0eb17'
$Root = Split-Path -Parent $PSScriptRoot
$External = Join-Path $Root 'external'
$SdkRoot = Join-Path $External 'foobar2000-sdk'
$PinMarker = Join-Path $SdkRoot '.sdk-pin.sha256'
$TreeMarker = Join-Path $SdkRoot '.sdk-template-tree.sha1'
$TemplateCheckout = Join-Path $External 'sdk-template'
$Readme = Join-Path $SdkRoot 'sdk-readme.html'
$Entry = Join-Path $SdkRoot 'foobar2000\SDK\foobar2000.h'
$Archive = Join-Path $External "SDK-$SdkVersion.7z"
$Stage = Join-Path $External "foobar2000-sdk-$SdkVersion-staging"

$hasOfficialArchivePin = (Test-Path -LiteralPath $PinMarker) -and
    ((Get-Content -LiteralPath $PinMarker -Raw).Trim() -eq $ExpectedSHA256)
$hasTemplateTreePin = (Test-Path -LiteralPath $TreeMarker) -and
    ((Get-Content -LiteralPath $TreeMarker -Raw).Trim() -eq $TemplateSdkTree)
if ((Test-Path -LiteralPath $Entry) -and
    (Test-Path -LiteralPath $Readme) -and
    ($hasOfficialArchivePin -or $hasTemplateTreePin)) {
    $readmeText = Get-Content -LiteralPath $Readme -Raw
    if ($readmeText.Contains("foobar2000 SDK, version $SdkVersion")) {
        Write-Host "Verified foobar2000 SDK $SdkVersion already installed (pinned source)."
        exit 0
    }
}
New-Item -ItemType Directory -Force -Path $External | Out-Null
$command = Get-Command '7z.exe' -ErrorAction SilentlyContinue
$SevenZip = if ($command) { $command.Source } else {
    Join-Path $env:ProgramFiles '7-Zip\7z.exe'
}
if (-not (Test-Path -LiteralPath $SevenZip)) {
    throw '7-Zip is required to verify/extract the official foobar2000 SDK.'
}
$SdkUrl = "https://www.foobar2000.org/downloads/SDK-$SdkVersion.7z"
# CI always checks out the immutable owner SDK tree. Prefer it directly
# instead of requesting the same official archive on every Win32/x64 build.
$preferPinnedTemplate = Test-Path -LiteralPath (Join-Path $TemplateCheckout '.git')
$downloaded = (-not $preferPinnedTemplate) -and (Test-Path -LiteralPath $Archive)
$lastTransportError = ''
if (-not $downloaded -and -not $preferPinnedTemplate) {
    # Use the official download only for a standalone local environment
    # without the verified shared owner SDK checkout.
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $SdkUrl -OutFile $Archive -MaximumRedirection 5 -TimeoutSec 35
            $downloaded = $true
            break
        } catch {
            $lastTransportError = $_.Exception.Message
            Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
            if ($attempt -lt 3) {
                Write-Warning "Official SDK transport unavailable (attempt $attempt of 3); retrying."
                Start-Sleep -Seconds (2 * $attempt)
            }
        }
    }
}
if (Test-Path -LiteralPath $Stage) {
    Remove-Item -LiteralPath $Stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
if ($downloaded) {
    # A received archive is NEVER replaced by the template fallback on any
    # hash, size, format, version or extraction mismatch. Fail closed.
    $size = (Get-Item -LiteralPath $Archive).Length
    $hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($size -ne $ExpectedSize -or $hash -ne $ExpectedSHA256) {
        Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
        throw ("SDK archive integrity failed: expected $ExpectedSize bytes / " +
               "$ExpectedSHA256; got $size bytes / $hash.")
    }
    & $SevenZip t $Archive | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Official SDK 7z integrity check failed.' }
    & $SevenZip x $Archive "-o$Stage" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Official SDK extraction failed.' }
} else {
    # Only a TRANSPORT failure may activate the owner-controlled, immutable
    # template Git tree. This checkout is neither an unsigned SDK archive
    # mirror nor a secret-bearing cache/artifact. A changed ref/tree fails.
    if (-not (Test-Path -LiteralPath (Join-Path $TemplateCheckout '.git'))) {
        throw "Official SDK transport failed ($lastTransportError); pinned template checkout absent."
    }
    $git = Get-Command git.exe -ErrorAction Stop
    $actualCommit = (& $git.Source -C $TemplateCheckout rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualCommit -cne $TemplateCommit) {
        throw 'Pinned SDK template Git commit does not match.'
    }
    $actualTree = (& $git.Source -C $TemplateCheckout rev-parse 'HEAD:vendor/sdk').Trim()
    if ($LASTEXITCODE -ne 0 -or $actualTree -cne $TemplateSdkTree) {
        throw 'Pinned SDK template tree hash does not match.'
    }
    $templatePin = Get-Content -LiteralPath (Join-Path $TemplateCheckout 'SDK-PIN.json') -Raw |
        ConvertFrom-Json
    if ($templatePin.sdk_version -cne $SdkVersion -or
        $templatePin.upstream_archive_sha256 -cne $ExpectedSHA256 -or
        [int64]$templatePin.archive_size_bytes -ne $ExpectedSize -or
        $templatePin.upstream_url -cne $SdkUrl) {
        throw 'Template SDK provenance disagrees with official archive pin.'
    }
    $sdkFiles = Join-Path $TemplateCheckout 'vendor\sdk'
    if (-not (Test-Path -LiteralPath (Join-Path $sdkFiles 'sdk-license.txt'))) {
        throw 'Pinned template SDK license missing.'
    }
    Copy-Item -Path (Join-Path $sdkFiles '*') -Destination $Stage -Recurse -Force
    Write-Host "FOOBAR_SDK_OWNER_TEMPLATE_PREFERRED=PASS (no vendor HTTP request; Git tree verified)"
    Write-Host 'SDK_ORIGINAL_ARCHIVE_SHA256=NOT_TESTED (Git tree source provenance instead)'
}
foreach ($relative in @(
    'foobar2000\SDK\foobar2000.h',
    'foobar2000\SDK\foobar2000_SDK.vcxproj',
    'foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj',
    'pfc\pfc.vcxproj',
    'sdk-readme.html',
    'sdk-license.txt'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $Stage $relative))) {
        throw "Pinned SDK source is missing required file $relative."
    }
}
$versionText = Get-Content -LiteralPath (Join-Path $Stage 'sdk-readme.html') -Raw
if (-not $versionText.Contains("foobar2000 SDK, version $SdkVersion")) {
    throw 'Pinned SDK source readme does not match the pinned version.'
}
if ($downloaded) {
    Set-Content -LiteralPath (Join-Path $Stage '.sdk-pin.sha256') -Value $ExpectedSHA256 -NoNewline -Encoding ascii
} else {
    Set-Content -LiteralPath (Join-Path $Stage '.sdk-template-tree.sha1') -Value $TemplateSdkTree -NoNewline -Encoding ascii
}
if (Test-Path -LiteralPath $SdkRoot) {
    Remove-Item -LiteralPath $SdkRoot -Recurse -Force
}
Move-Item -LiteralPath $Stage -Destination $SdkRoot
if ($downloaded) {
    Write-Host "PASS: official foobar2000 SDK $SdkVersion installed (archive SHA-256 verified)."
} else {
    Write-Host "PASS: foobar2000 SDK $SdkVersion installed (owner template Git tree verified)."
}
