[CmdletBinding()]
param(
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$checksumPath = Join-Path $PSScriptRoot 'SHA256SUMS.txt'
$lines = Get-Content -LiteralPath $checksumPath | Where-Object { $_.Trim().Length -gt 0 }
foreach ($line in $lines) {
    if ($line -notmatch '^([0-9A-F]{64})  (.+)$') {
        throw "Malformed checksum line: $line"
    }
    $expected = $Matches[1]
    $relative = $Matches[2].Replace('/', [IO.Path]::DirectorySeparatorChar)
    $path = Join-Path $PSScriptRoot $relative
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing snapshot file: $relative" }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    if ($actual -ne $expected) { throw "Hash mismatch for ${relative}: expected $expected, got $actual" }
}

$snapshot = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'snapshot.json') | ConvertFrom-Json
$branch = (& git.exe -C $PSScriptRoot branch --show-current).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Unable to determine the current Git branch.' }

if ($branch -eq $snapshot.localFeatureBranch -or $branch -eq $snapshot.chapter2Branch) {
    $candidate = $snapshot.localCandidateArtifact
    $sourceChecks = @(
        @{ Path = 'src\native_vr.cpp'; Expected = $candidate.source.nativeVrSha256 },
        @{ Path = 'src\feature_input.cpp'; Expected = $candidate.source.featureInputSha256 },
        @{ Path = 'src\feature_input.hpp'; Expected = $candidate.source.featureInputHeaderSha256 },
        @{ Path = 'src\feature_engine_input.cpp'; Expected = $candidate.source.engineInputSha256 },
        @{ Path = 'src\feature_engine_input.hpp'; Expected = $candidate.source.engineInputHeaderSha256 },
        @{ Path = 'src\feature_startup_menu.cpp'; Expected = $candidate.source.startupMenuSha256 },
        @{ Path = 'src\feature_startup_menu.hpp'; Expected = $candidate.source.startupMenuHeaderSha256 },
        @{ Path = 'source\NativeVR\src\vr_hands.cpp'; Expected = $candidate.source.vrHandsSha256 },
        @{ Path = 'source\NativeVR\src\vr_tools.cpp'; Expected = $candidate.source.vrToolsSha256 },
        @{ Path = 'source\NativeVR\src\mechanic_hands_asset.hpp'; Expected = $candidate.source.mechanicHandsAssetSha256 },
        @{ Path = 'source\NativeVR\src\native_tool_asset.hpp'; Expected = $candidate.source.nativeToolAssetSha256 }
    )
    foreach ($check in $sourceChecks) {
        $sourcePath = Join-Path $PSScriptRoot $check.Path
        $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath).Hash
        if ($sourceHash -ne $check.Expected) {
            throw "Feature source hash mismatch for $($check.Path): expected $($check.Expected), got $sourceHash"
        }
    }

    $artifactPath = Join-Path $PSScriptRoot $candidate.path
    $artifact = Get-Item -LiteralPath $artifactPath
    $artifactHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath).Hash
    if ($artifact.Length -ne $candidate.size -or $artifactHash -ne $candidate.sha256) {
        throw "Feature artifact mismatch: expected $($candidate.size) bytes / $($candidate.sha256), got $($artifact.Length) bytes / $artifactHash"
    }
} else {
    $sourcePath = Join-Path $PSScriptRoot $snapshot.source.path
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath).Hash
    if ($sourceHash -ne $snapshot.source.sha256) {
        throw "Stable source hash mismatch: expected $($snapshot.source.sha256), got $sourceHash"
    }
}

if ($GamePath) {
    $exe = Join-Path $GamePath 'Release\ScrapMechanic.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw "Game executable not found: $exe" }
    $gameHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash
    $expectedGame = '5D663BA2EC5DC8C7ABEFCC5C9344AE86F0A066C4069A91F54833524AC9A5B4F5'
    if ($gameHash -ne $expectedGame) {
        throw "Unsupported ScrapMechanic.exe: expected $expectedGame, got $gameHash"
    }
}

Write-Host "Snapshot payload and source hashes verified for branch $branch."
if ($GamePath) { Write-Host "Supported Scrap Mechanic build verified: Steam build 24529696." }
