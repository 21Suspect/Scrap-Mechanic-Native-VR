[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$GamePath,
    [switch]$IncludeBootstrap
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $GamePath).Path
$manifestPath = Join-Path $PSScriptRoot 'manifest.json'
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$exe = Join-Path $root $manifest.game.executable
if (-not (Test-Path -LiteralPath $exe)) { throw 'GamePath does not contain ScrapMechanic.exe.' }
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash -ne $manifest.game.executableSha256) {
    throw 'Refusing to sync from an unsupported Scrap Mechanic build.'
}

$backupRoot = Join-Path $PSScriptRoot ('payload-backups\' + (Get-Date -Format 'yyyyMMddTHHmmss'))
foreach ($entry in $manifest.files) {
    if (-not $IncludeBootstrap -and $entry.module -eq 'vr-bootstrap') { continue }
    $source = Join-Path $root $entry.path
    $payload = Join-Path $PSScriptRoot ('payload\' + $entry.path)
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Installed source file is missing: $($entry.path)"
    }
    if (Test-Path -LiteralPath $payload) {
        $backup = Join-Path $backupRoot $entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
        Copy-Item -LiteralPath $payload -Destination $backup -Force
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $payload) | Out-Null
    Copy-Item -LiteralPath $source -Destination $payload -Force
    $entry.patchedSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $payload).Hash
    Write-Host "Synced $($entry.path)"
}

$temporary = "$manifestPath.tmp"
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temporary -Encoding UTF8
Move-Item -LiteralPath $temporary -Destination $manifestPath -Force
Write-Host 'Payload and patched hashes updated.' -ForegroundColor Green
Write-Host "Previous payload backup: $backupRoot" -ForegroundColor Green
& (Join-Path $PSScriptRoot 'Patcher.ps1') -Action ValidatePayload
