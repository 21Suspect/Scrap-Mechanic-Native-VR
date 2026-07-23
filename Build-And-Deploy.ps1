[CmdletBinding()]
param(
    [string]$GamePath,
    [switch]$UpdateDistribution
)

$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path $PSScriptRoot 'manifest.json'
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$sourceRoot = Join-Path $PSScriptRoot 'source\NativeVR'
$buildScript = Join-Path $sourceRoot 'Build.ps1'
$builtAddon = Join-Path $sourceRoot 'build\scrap_native_vr.addon64'

if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot '.tools') -PathType Container) -and
    -not (Get-Command zig.exe -ErrorAction SilentlyContinue) -and
    -not $env:SCRAPVR_ZIG) {
    & (Join-Path $PSScriptRoot 'Get-Dependencies.ps1')
}

& $buildScript
if (-not (Test-Path -LiteralPath $builtAddon -PathType Leaf)) {
    throw 'The build completed without producing scrap_native_vr.addon64.'
}
$builtHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $builtAddon).Hash

function Find-Game([string]$Explicit) {
    $candidates = @(
        $Explicit,
        (Split-Path -Parent $PSScriptRoot),
        'C:\Program Files (x86)\Steam\steamapps\common\Scrap Mechanic',
        'C:\Program Files\Steam\steamapps\common\Scrap Mechanic'
    ) | Where-Object { $_ }
    foreach ($candidate in $candidates) {
        $exe = Join-Path $candidate 'Release\ScrapMechanic.exe'
        if (Test-Path -LiteralPath $exe -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Could not find Scrap Mechanic. Pass -GamePath explicitly.'
}

$root = Find-Game $GamePath
$exe = Join-Path $root $manifest.game.executable
$exeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash
if ($exeHash -ne $manifest.game.executableSha256) {
    throw "Refusing to deploy to an unsupported executable: $exeHash"
}
if (Get-Process -Name ScrapMechanic -ErrorAction SilentlyContinue) {
    throw 'Close Scrap Mechanic before deploying a development build.'
}

$target = Join-Path $root 'Release\scrap_native_vr.addon64'
$backupRoot = Join-Path $PSScriptRoot ('dev-backups\' + (Get-Date -Format 'yyyyMMddTHHmmss'))
New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
if (Test-Path -LiteralPath $target) {
    Copy-Item -LiteralPath $target -Destination (Join-Path $backupRoot 'scrap_native_vr.addon64')
}
Copy-Item -LiteralPath $builtAddon -Destination $target -Force
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash -ne $builtHash) {
    throw 'Development add-on deployment hash verification failed.'
}
Write-Host "Deployed development add-on SHA-256 $builtHash" -ForegroundColor Green
Write-Host "Previous binary backup: $backupRoot" -ForegroundColor Green

if ($UpdateDistribution) {
    $payloadAddon = Join-Path $PSScriptRoot 'payload\Release\scrap_native_vr.addon64'
    Copy-Item -LiteralPath $builtAddon -Destination $payloadAddon -Force
    $addonEntry = $manifest.files | Where-Object path -EQ 'Release\scrap_native_vr.addon64'
    if (-not $addonEntry) { throw 'Add-on entry is missing from manifest.json.' }
    $addonEntry.patchedSha256 = $builtHash
    $temporary = "$manifestPath.tmp"
    $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $manifestPath -Force
    Write-Host 'Updated the distributable payload and manifest to the new build.' -ForegroundColor Green
}
