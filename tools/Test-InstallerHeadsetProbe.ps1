[CmdletBinding()]
param([string]$InstallerPath)

$ErrorActionPreference = 'Stop'
if (-not $InstallerPath) {
    $InstallerPath = Join-Path $PSScriptRoot '..\dist\ScrapMechanicVR-Installer.exe'
}
$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
$assembly = [Reflection.Assembly]::LoadFile($installer)
$probeType = $assembly.GetType('ScrapMechanicVRPatcher.OpenXrProbe', $true)
$method = $probeType.GetMethod('HeadsetAvailableWithTimeout', [Reflection.BindingFlags]'Static,NonPublic')
$arguments = [object[]]@('')
$available = [bool]$method.Invoke($null, $arguments)
Write-Host "OpenXR headset available: $available"
Write-Host "OpenXR detail: $($arguments[0])"
