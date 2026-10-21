$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Official SDK pin qualified independently in
# lxsdd/foobar2000_component_template SDK-PIN.json.
$SdkVersion = '2026-10-01'
$ExpectedSize = 793738
$ExpectedSHA256 = 'd4c55077336fae81bf8df0259b5b2748fa45ea84132c656ead93eb123cbcdc26'
$Root = Split-Path -Parent $PSScriptRoot
$External = Join-Path $Root 'external'
$SdkRoot = Join-Path $External 'foobar2000-sdk'
$PinMarker = Join-Path $SdkRoot '.sdk-pin.sha256'
$Readme = Join-Path $SdkRoot 'sdk-readme.html'
$Entry = Join-Path $SdkRoot 'foobar2000\SDK\foobar2000.h'
$Archive = Join-Path $External "SDK-$SdkVersion.7z"
$Stage = Join-Path $External "foobar2000-sdk-$SdkVersion-staging"

if ((Test-Path -LiteralPath $Entry) -and
    (Test-Path -LiteralPath $Readme) -and
    (Test-Path -LiteralPath $PinMarker) -and
    ((Get-Content -LiteralPath $PinMarker -Raw).Trim() -eq $ExpectedSHA256)) {
    $readmeText = Get-Content -LiteralPath $Readme -Raw
    if ($readmeText.Contains("foobar2000 SDK, version $SdkVersion")) {
        Write-Host "Verified foobar2000 SDK pin $SdkVersion already installed."
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
if (-not (Test-Path -LiteralPath $Archive)) {
    # Fail closed: never accept a partial archive from a failed transport.
    # Retry only network failures. The pinned size, SHA-256 and 7z archive
    # verification below remain mandatory and never retry a hash mismatch.
    $downloaded = $false
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $SdkUrl -OutFile $Archive -MaximumRedirection 5 -TimeoutSec 35
            $downloaded = $true
            break
        } catch {
            Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
            if ($attempt -eq 3) {
                throw "Pinned official SDK download failed after 3 attempts: $($_.Exception.Message)"
            }
            Write-Warning "Official SDK transport unavailable (attempt $attempt of 3); retrying without accepting any partial archive."
            Start-Sleep -Seconds (2 * $attempt)
        }
    }
    if (-not $downloaded) { throw 'Pinned official SDK download incomplete.' }
}
$size = (Get-Item -LiteralPath $Archive).Length
$hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($size -ne $ExpectedSize -or $hash -ne $ExpectedSHA256) {
    Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
    throw ("SDK archive integrity failed: expected $ExpectedSize bytes / " +
           "$ExpectedSHA256; got $size bytes / $hash.")
}
& $SevenZip t $Archive | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Official SDK 7z integrity check failed.' }
# Extract into staging; only replace an existing SDK after all checks pass.
if (Test-Path -LiteralPath $Stage) {
    Remove-Item -LiteralPath $Stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
& $SevenZip x $Archive "-o$Stage" -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Official SDK extraction failed.' }
foreach ($relative in @(
    'foobar2000\SDK\foobar2000.h',
    'foobar2000\SDK\foobar2000_SDK.vcxproj',
    'foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj',
    'pfc\pfc.vcxproj',
    'sdk-readme.html',
    'sdk-license.txt'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $Stage $relative))) {
        throw "Official SDK archive is missing required file $relative."
    }
}
$versionText = Get-Content -LiteralPath (Join-Path $Stage 'sdk-readme.html') -Raw
if (-not $versionText.Contains("foobar2000 SDK, version $SdkVersion")) {
    throw 'SDK archive readme does not match the pinned version.'
}
Set-Content -LiteralPath (Join-Path $Stage '.sdk-pin.sha256') -Value $ExpectedSHA256 -NoNewline -Encoding ascii
if (Test-Path -LiteralPath $SdkRoot) {
    Remove-Item -LiteralPath $SdkRoot -Recurse -Force
}
Move-Item -LiteralPath $Stage -Destination $SdkRoot
Write-Host "PASS: official foobar2000 SDK $SdkVersion installed (SHA-256 pinned)."
