param(
    [Parameter(Mandatory = $true)][string]$Win32Dll,
    [Parameter(Mandatory = $true)][string]$X64Dll,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Commit
)
$ErrorActionPreference = 'Stop'
function Get-PeMachine([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path))
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "$Path is not a valid PE file." }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or ($peOffset + 6) -gt $bytes.Length) { throw "$Path has an invalid PE header offset." }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "$Path has no PE signature."
    }
    return [BitConverter]::ToUInt16($bytes, $peOffset + 4)
}
$win32Machine = Get-PeMachine $Win32Dll
$x64Machine = Get-PeMachine $X64Dll
if ($win32Machine -ne 0x014c) { throw ("Win32 DLL has PE machine 0x{0:X4}; expected 0x014C." -f $win32Machine) }
if ($x64Machine -ne 0x8664) { throw ("x64 DLL has PE machine 0x{0:X4}; expected 0x8664." -f $x64Machine) }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$stage = Join-Path $OutputDirectory 'component-stage'
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'x64') | Out-Null
Copy-Item -LiteralPath $Win32Dll -Destination (Join-Path $stage 'foo_music_metadata_studio.dll')
Copy-Item -LiteralPath $X64Dll -Destination (Join-Path $stage 'x64\foo_music_metadata_studio.dll')
$zip = Join-Path $OutputDirectory ("foo_music_metadata_studio-" + $Version + ".zip")
$component = Join-Path $OutputDirectory ("foo_music_metadata_studio-" + $Version + ".fb2k-component")
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
if (Test-Path $component) { Remove-Item -LiteralPath $component -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Move-Item -LiteralPath $zip -Destination $component
$win32Hash = (Get-FileHash -LiteralPath (Join-Path $stage 'foo_music_metadata_studio.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
$x64Hash = (Get-FileHash -LiteralPath (Join-Path $stage 'x64\foo_music_metadata_studio.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
$componentHash = (Get-FileHash -LiteralPath $component -Algorithm SHA256).Hash.ToLowerInvariant()
@(
    "version=$Version",
    "commit=$Commit",
    ("root_dll_machine=0x{0:X4}" -f $win32Machine),
    ("x64_dll_machine=0x{0:X4}" -f $x64Machine),
    "root_dll_sha256=$win32Hash",
    "x64_dll_sha256=$x64Hash",
    "component_sha256=$componentHash",
    "layout=root:foo_music_metadata_studio.dll;x64:x64/foo_music_metadata_studio.dll",
    "analysis_only=true",
    "status=PASS"
) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'BUILD-MANIFEST.txt') -Encoding UTF8
("$componentHash  " + (Split-Path -Leaf $component)) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding ASCII
Write-Host "PASS: foobar2000 component layout and PE architecture verified."
Write-Host "Component: $component"
