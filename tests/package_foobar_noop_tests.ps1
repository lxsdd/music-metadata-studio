#requires -Version 5.1
# Synthetic PE-only package QA. Never publishes or installs components.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Join-Path ([IO.Path]::GetTempPath()) ('mms-package-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    function New-Dummy-Pe([string]$FilePath,[uint16]$Machine) {
        $bytes = New-Object byte[] 160
        $bytes[0] = 0x4d; $bytes[1] = 0x5a
        [BitConverter]::GetBytes([int]128).CopyTo($bytes, 0x3c)
        $bytes[128] = 0x50; $bytes[129] = 0x45
        [BitConverter]::GetBytes($Machine).CopyTo($bytes, 132)
        [IO.File]::WriteAllBytes($FilePath,$bytes)
    }
    $win32 = Join-Path $root 'synthetic-win32.dll'
    $x64 = Join-Path $root 'synthetic-x64.dll'
    New-Dummy-Pe $win32 ([uint16]0x014c)
    New-Dummy-Pe $x64 ([uint16]0x8664)
    $source32 = (Get-FileHash -LiteralPath $win32 -Algorithm SHA256).Hash
    $source64 = (Get-FileHash -LiteralPath $x64 -Algorithm SHA256).Hash
    $packageScript = Join-Path $PSScriptRoot '..\scripts\package-foobar.ps1'
    $sha = '0123456789abcdef0123456789abcdef01234567'
    $version = '0.0.0-test'
    $out = Join-Path $root 'output'
    & $packageScript -Win32Dll $win32 -X64Dll $x64 -OutputDirectory $out -Version $version -Commit $sha
    $package = Join-Path $out 'foo_music_metadata_studio-0.0.0-test.fb2k-component'
    $manifest = Join-Path $out 'BUILD-MANIFEST.txt'
    $sums = Join-Path $out 'SHA256SUMS.txt'
    foreach ($f in @($package,$manifest,$sums)) {
        if (-not (Test-Path -LiteralPath $f -PathType Leaf)) {
            throw "Missing package output: $f"
        }
    }
    $proof = Get-Content -LiteralPath $manifest -Raw
    foreach ($expected in @("commit=$sha",'root_dll_machine=0x014C','x64_dll_machine=0x8664',
                              'analysis_only=false','write_mode=readonly',
                              'write_mode_scope=physical-tags-and-studio-existing-cue-writer',
                              'physical_tags_write_enabled=false',
                              'external_cue_write_enabled=true',
                              'external_cue_write_scope=precision-new-sibling-create-only',
                              'studio_existing_cue_value_writer_enabled=false',
                              'precision_cue_create_only_enabled=true',
                              'precision_cue_create_scope=reviewed-new-sibling-only-explicit-confirmation-no-replace',
                              'precision_cue_multi_disc_create_enabled=true',
                              'embedded_cue_write_enabled=false',
                              'file_operations_write_enabled=false','status=PASS')) {
        if (-not $proof.Contains($expected)) { throw "Manifest missing $expected" }
    }
    $hash = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not (Get-Content -LiteralPath $sums -Raw).Trim().StartsWith($hash + '  ')) {
        throw 'SHA256 sidecar mismatch'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($package)
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
        foreach ($required in @('foo_music_metadata_studio.dll','x64/foo_music_metadata_studio.dll')) {
            if ($required -cnotin $names) { throw "Invalid component layout: $required" }
        }
        if ($names.Count -ne 2) { throw 'Unexpected archive contents' }
    } finally { $zip.Dispose() }
    if ((Get-FileHash -LiteralPath $win32 -Algorithm SHA256).Hash -cne $source32 -or
        (Get-FileHash -LiteralPath $x64 -Algorithm SHA256).Hash -cne $source64) {
        throw 'Synthetic source DLL mutated during packaging'
    }
    # The opt-in private variant must advertise its real write boundary.
    $writeOut = Join-Path $root 'physical-tags-output'
    & $packageScript -Win32Dll $win32 -X64Dll $x64 -OutputDirectory $writeOut -Version $version -Commit $sha -WriteMode 'physical-tags'
    $writeManifest = Get-Content -LiteralPath (Join-Path $writeOut 'BUILD-MANIFEST.txt') -Raw
    foreach ($expected in @('analysis_only=false','write_mode=physical-tags',
                              'physical_tags_write_enabled=true',
                              'external_cue_write_enabled=true',
                              'external_cue_write_scope=precision-new-sibling-create-only+studio-value-only-exact-edition',
                              'studio_existing_cue_value_writer_enabled=true',
                              'precision_cue_create_only_enabled=true',
                              'precision_cue_create_scope=reviewed-new-sibling-only-explicit-confirmation-no-replace',
                              'precision_cue_multi_disc_create_enabled=true',
                              'embedded_cue_write_enabled=false',
                              'file_operations_write_enabled=false','status=PASS')) {
        if (-not $writeManifest.Contains($expected)) { throw "Write-mode manifest missing $expected" }
    }
    foreach ($script in @('build-foobar.ps1','local-acceptance-build.ps1','package-foobar.ps1')) {
        $scriptPath = Join-Path $PSScriptRoot ("..\\scripts\\" + $script)
        $tokens = $null
        $parseErrors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile(
            $scriptPath,[ref]$tokens,[ref]$parseErrors)
        if ($parseErrors.Count -ne 0) { throw "PowerShell syntax invalid: $script" }
    }
    $wrongRejected = $false
    try {
        & $packageScript -Win32Dll $x64 -X64Dll $win32 -OutputDirectory (Join-Path $root 'wrong') -Version $version -Commit $sha
    } catch {
        if ($_.Exception.Message -match 'Win32 DLL has PE machine') {
            $wrongRejected = $true
        } else { throw }
    }
    if (-not $wrongRejected) { throw 'Swapped architectures were accepted' }
    Write-Host 'PASS: synthetic Studio package layout, manifest, SHA256 and unsafe machine refusal'
} finally {
    Remove-Item -LiteralPath $root -Force -Recurse -ErrorAction SilentlyContinue
}
