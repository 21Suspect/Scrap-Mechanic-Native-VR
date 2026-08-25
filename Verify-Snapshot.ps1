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

$sourcePath = Join-Path $PSScriptRoot 'src\native_vr.cpp'
$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath).Hash
$expectedSource = '678F882BBE9DED0611D2EA01B2DF8DDC4F4BC391D258F499B50CC7CCB92C383A'
if ($sourceHash -ne $expectedSource) {
    throw "Source hash mismatch: expected $expectedSource, got $sourceHash"
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

Write-Host "Snapshot payload and source hashes verified."
if ($GamePath) { Write-Host "Supported Scrap Mechanic build verified: Steam build 24529696." }
