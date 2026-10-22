# Personal source-only acceptance build. Never installs into foobar or uploads binaries.
param(
    [string]$SourceSha = '',
    [string]$OutputDirectory = '',
    [switch]$PreflightOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'Windows 10/11 required.' }
$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $msbuild -or -not $git) {
    throw 'Preflight: VS2022 C++ MSBuild and Git are required (pinned SDK comes from GitHub). 7-Zip and manufacturer downloads are not required. No installation attempted.'
}
# Runs standalone from any directory, including a single downloaded script
# under $env:TEMP. Source project files are checked after exact-SHA download.
if ($PreflightOnly) {
    Write-Host 'LOCAL_ACCEPTANCE_PREFLIGHT=PASS; no download, build, installation or personal data touched.'
    return
}
if ($SourceSha -cnotmatch '^[a-f0-9]{40}$') { throw 'Pass exact lowercase 40-digit Git SHA.' }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { throw 'Explicit -OutputDirectory required.' }
$destination = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ("MMS-local-" + $SourceSha)
if (Test-Path -LiteralPath $destination) { throw 'Output already exists: refuse overwrite.' }
$temp = Join-Path ([IO.Path]::GetTempPath()) ('m-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $zip = Join-Path $temp 'source.zip'
    $sourceDir = Join-Path $temp 'source'
    Invoke-WebRequest -UseBasicParsing -MaximumRedirection 5 -Uri ('https://codeload.github.com/lxsdd/music-metadata-studio/zip/' + $SourceSha) -OutFile $zip
    Expand-Archive -LiteralPath $zip -DestinationPath $sourceDir
    $roots = @(Get-ChildItem -LiteralPath $sourceDir -Directory)
    if ($roots.Count -ne 1) { throw 'GitHub archive must contain exactly one source directory.' }
    $expectedName = 'music-metadata-studio-'
    $actualName = $roots[0].Name
    if (-not ($actualName -eq ($expectedName + $SourceSha) -or
               $actualName -eq ($expectedName + $SourceSha.Substring(0,7)))) {
        throw "Source archive root does not match pinned Git SHA: $actualName"
    }
    # Keep MSBuild/PFC file tracker paths below legacy Windows MAX_PATH.
    # Preserve SHA provenance through the root validation above, then relocate.
    $root = Join-Path $temp 'r'
    Move-Item -LiteralPath $roots[0].FullName -Destination $root
    foreach ($f in @('scripts\build-foobar.ps1','scripts\package-foobar.ps1',
                     'scripts\bootstrap-sdk.ps1','VERSION','foo_music_metadata_studio.vcxproj')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $f) -PathType Leaf)) {
            throw "Pinned source missing $f"
        }
    }
    # Keep the exact-SHA source build non-public and use only pinned SDK
    # acquisition rules within this repository's own bootstrap script.
    $build = Join-Path $root 'scripts\build-foobar.ps1'
    & $build -Platform Win32 -Configuration Release -PlatformToolset v143
    if ($LASTEXITCODE -ne 0) { throw 'Win32 build failed.' }
    & $build -Platform x64 -Configuration Release -PlatformToolset v143
    if ($LASTEXITCODE -ne 0) { throw 'x64 build failed.' }
    $dll32 = Join-Path $root 'out\foobar\Win32\Release\foo_music_metadata_studio.dll'
    $dll64 = Join-Path $root 'out\foobar\x64\Release\foo_music_metadata_studio.dll'
    foreach ($p in @($dll32,$dll64)) {
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing compiled DLL $p" }
    }
    $version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
    if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+([-.][A-Za-z0-9.-]+)?$') {
        throw 'Unexpected version string.'
    }
    $stage = Join-Path $temp 'package'
    & (Join-Path $root 'scripts\package-foobar.ps1') -Win32Dll $dll32 -X64Dll $dll64 -OutputDirectory $stage -Version $version -Commit $SourceSha
    $file = Join-Path $stage ("foo_music_metadata_studio-" + $version + ".fb2k-component")
    $manifest = Join-Path $stage 'BUILD-MANIFEST.txt'
    $sums = Join-Path $stage 'SHA256SUMS.txt'
    foreach ($p in @($file,$manifest,$sums)) {
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw 'Incomplete package.' }
    }
    $proof = Get-Content -LiteralPath $manifest -Raw
    foreach ($value in @("commit=$SourceSha",'root_dll_machine=0x014C',
                         'x64_dll_machine=0x8664','status=PASS')) {
        if (-not $proof.Contains($value)) { throw "Missing manifest proof: $value" }
    }
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    $sidecar = (Get-Content -LiteralPath $sums -Raw).Trim()
    if (-not $sidecar.StartsWith($hash + '  ')) { throw 'SHA256 sidecar mismatch.' }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    if (Test-Path -LiteralPath $destination) { throw 'Destination appeared; refuse overwrite.' }
    New-Item -ItemType Directory -Path $destination | Out-Null
    Copy-Item -LiteralPath $file,$manifest,$sums -Destination $destination
    $copiedHash = (Get-FileHash -LiteralPath (Join-Path $destination (Split-Path -Leaf $file)) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($copiedHash -ne $hash) { throw 'Local copy checksum mismatch.' }
    Write-Host "LOCAL_ACCEPTANCE_BUILD=PASS PATH=$destination SOURCE_SHA=$SourceSha SHA256=$hash"
    Write-Host 'No installation, tag/database/music writes or public GitHub binary artifacts.'
} finally {
    if (Test-Path -LiteralPath $temp) {
        Remove-Item -LiteralPath $temp -Force -Recurse -ErrorAction SilentlyContinue
    }
}
