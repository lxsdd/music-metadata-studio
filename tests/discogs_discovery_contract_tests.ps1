$ErrorActionPreference='Stop'
& "$PSScriptRoot/../scripts/qa-discogs-discovery.ps1" -Mode SelfTest
if (-not $?) { throw 'Protected Discogs discovery contract failed' }
