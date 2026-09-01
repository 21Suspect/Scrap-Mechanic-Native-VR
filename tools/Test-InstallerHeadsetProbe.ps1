[CmdletBinding()]
param(
    [string]$InstallerPath,
    [switch]$IsolatedDependencySearch
)

$ErrorActionPreference = 'Stop'
if (-not $InstallerPath) {
    $InstallerPath = Join-Path $PSScriptRoot '..\dist\ScrapMechanicVR-Installer.exe'
}
$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
if ($IsolatedDependencySearch) {
    $diagnosticRoot = Join-Path $env:LOCALAPPDATA 'ScrapMechanicVR-Chapter2\diagnostics'
    New-Item -ItemType Directory -Force -Path $diagnosticRoot | Out-Null
    $resultPath = Join-Path $diagnosticRoot ("loader-isolation-$([Guid]::NewGuid().ToString('N')).txt")
    try {
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $installer
        $info.Arguments = "--headset-probe-worker `"$resultPath`""
        $info.WorkingDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        # Deliberately exclude LLVM-MinGW and the game directory. The packaged
        # loader must resolve its adjacent dependencies without help from PATH.
        $info.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
        $process = [Diagnostics.Process]::Start($info)
        $process.WaitForExit()
        if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
            throw 'The isolated OpenXR probe returned no result.'
        }
        $result = Get-Content -Raw -LiteralPath $resultPath
        Write-Host "Isolated OpenXR worker exit code: $($process.ExitCode)"
        Write-Host $result.TrimEnd()
        if ($result -match 'Windows error 126') {
            throw 'The packaged OpenXR loader still depends on an external DLL search path.'
        }
        Write-Host 'PASS: packaged OpenXR loader resolved all adjacent dependencies without PATH assistance.' -ForegroundColor Green
        return
    }
    finally {
        Remove-Item -LiteralPath $resultPath -Force -ErrorAction SilentlyContinue
    }
}

$assembly = [Reflection.Assembly]::LoadFile($installer)
$probeType = $assembly.GetType('ScrapMechanicVRPatcher.OpenXrProbe', $true)
$method = $probeType.GetMethod('HeadsetAvailableWithTimeout', [Reflection.BindingFlags]'Static,NonPublic')
$arguments = [object[]]@('')
$available = [bool]$method.Invoke($null, $arguments)
Write-Host "OpenXR headset available: $available"
Write-Host "OpenXR detail: $($arguments[0])"
