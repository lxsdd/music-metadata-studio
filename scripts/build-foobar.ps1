param(
    [ValidateSet('Win32','x64')] [string]$Platform = 'x64',
    [ValidateSet('Debug','Release')] [string]$Configuration = 'Release',
    [string]$PlatformToolset = 'v143'
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'bootstrap-sdk.ps1')
$MsBuildCommand = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if (-not $MsBuildCommand) { throw 'MSBuild not found. Install Visual Studio 2022 C++ tools.' }
$MsBuild = $MsBuildCommand.Source
& $MsBuild (Join-Path $Root 'foo_music_metadata_studio.vcxproj') /m /p:Configuration=$Configuration /p:Platform=$Platform /p:PlatformToolset=$PlatformToolset /restore
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
