[CmdletBinding()]
param(
    [ValidateRange(10, 180)]
    [int]$WaitForLinkSeconds = 90
)

$ErrorActionPreference = 'Stop'
$gameRoot = Split-Path -Parent $PSScriptRoot
$release = Join-Path $gameRoot 'Release'
$game = Join-Path $release 'ScrapMechanic.exe'
$addon = Join-Path $release 'scrap_native_vr.addon64'

function Get-ActiveOpenXrRuntime {
    foreach ($key in @(
        'HKLM:\SOFTWARE\Khronos\OpenXR\1',
        'HKLM:\SOFTWARE\WOW6432Node\Khronos\OpenXR\1'
    )) {
        try {
            $runtime = (Get-ItemProperty -LiteralPath $key -Name ActiveRuntime -ErrorAction Stop).ActiveRuntime
            if ($runtime) {
                return [Environment]::ExpandEnvironmentVariables($runtime)
            }
        }
        catch {}
    }
    return $null
}

function Find-MetaClient([string]$RuntimePath) {
    $candidates = @()
    if ($RuntimePath) {
        $runtimeDirectory = Split-Path -Parent $RuntimePath
        $supportDirectory = Split-Path -Parent $runtimeDirectory
        $candidates += (Join-Path $supportDirectory 'oculus-client\Client.exe')
        $candidates += (Join-Path $supportDirectory 'oculus-client\OculusClient.exe')
    }
    $candidates += 'C:\Program Files\Meta Horizon\Support\oculus-client\Client.exe'
    $candidates += 'C:\Program Files\Oculus\Support\oculus-client\OculusClient.exe'
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return $null
}

function Set-StrictFollowVehicleCamera {
    $userRoot = Join-Path $env:APPDATA 'Axolot Games\Scrap Mechanic\User'
    if (-not (Test-Path -LiteralPath $userRoot -PathType Container)) {
        return
    }
    $backupRoot = Join-Path $env:LOCALAPPDATA 'ScrapMechanicVR\preferences'
    foreach ($profile in Get-ChildItem -LiteralPath $userRoot -Directory -Filter 'User_*' -ErrorAction SilentlyContinue) {
        $settingsPath = Join-Path $profile.FullName 'settings.json'
        if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
            continue
        }
        try {
            $raw = [IO.File]::ReadAllText($settingsPath)
            $settings = $raw | ConvertFrom-Json
            if ($settings.VehicleCameraMode -eq 3) {
                continue
            }
            New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
            $backupPath = Join-Path $backupRoot ($profile.Name + '-settings-before-strict-follow.json')
            if (-not (Test-Path -LiteralPath $backupPath)) {
                Copy-Item -LiteralPath $settingsPath -Destination $backupPath
            }
            if ($settings.PSObject.Properties.Name -contains 'VehicleCameraMode') {
                $settings.VehicleCameraMode = 3
            }
            else {
                $settings | Add-Member -NotePropertyName VehicleCameraMode -NotePropertyValue 3
            }
            $temporary = "$settingsPath.scrapvr.tmp"
            $json = $settings | ConvertTo-Json -Depth 10
            [IO.File]::WriteAllText($temporary, $json, (New-Object Text.UTF8Encoding($false)))
            Move-Item -LiteralPath $temporary -Destination $settingsPath -Force
            Write-Host "Vehicle camera set to Strict Follow for $($profile.Name)." -ForegroundColor Green
        }
        catch {
            Write-Host "Could not update vehicle camera preference for $($profile.Name): $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
}

if (-not (Test-Path -LiteralPath $game)) {
    throw "Scrap Mechanic executable not found: $game"
}
if (-not (Test-Path -LiteralPath $addon)) {
    throw 'The native VR add-on is not installed. Run Build.ps1 and Install-Bootstrap.ps1 first.'
}
if (Get-Process -Name ScrapMechanic -ErrorAction SilentlyContinue) {
    Write-Host 'Scrap Mechanic is already running.' -ForegroundColor Yellow
    return
}

$activeRuntime = Get-ActiveOpenXrRuntime
if (-not $activeRuntime -or -not (Test-Path -LiteralPath $activeRuntime -PathType Leaf)) {
    throw 'No active 64-bit OpenXR runtime was found. In Meta Horizon Link, open Settings > General and set Meta Quest Link as the active OpenXR runtime.'
}
Write-Host "Active OpenXR runtime: $activeRuntime"

if ($activeRuntime -match 'oculus|meta') {
    $metaClient = Find-MetaClient $activeRuntime
    $metaRunning = Get-Process -Name Client,OculusClient -ErrorAction SilentlyContinue
    if (-not $metaRunning) {
        if (-not $metaClient) {
            throw 'Meta Horizon Link is the active OpenXR runtime, but its desktop client could not be found. Repair or reinstall Meta Horizon Link.'
        }
        Start-Process -FilePath $metaClient
    }

    if (-not (Get-Process -Name OculusDash -ErrorAction SilentlyContinue)) {
        Write-Host 'Quest Link is not active. In Meta Horizon Link, select Stream/Link and keep the headset awake.' -ForegroundColor Yellow
        $deadline = (Get-Date).AddSeconds($WaitForLinkSeconds)
        do {
            Start-Sleep -Seconds 1
            $linkActive = Get-Process -Name OculusDash -ErrorAction SilentlyContinue
        } while (-not $linkActive -and (Get-Date) -lt $deadline)
        if (-not $linkActive) {
            throw 'Quest Link did not become active before the timeout.'
        }
    }
}
elseif ($activeRuntime -match 'steam') {
    if (-not (Get-Process -Name vrserver -ErrorAction SilentlyContinue)) {
        Write-Host 'Starting SteamVR for the active OpenXR runtime.' -ForegroundColor Yellow
        Start-Process 'steam://rungameid/250820'
        $deadline = (Get-Date).AddSeconds($WaitForLinkSeconds)
        do {
            Start-Sleep -Seconds 1
            $steamVrReady = Get-Process -Name vrserver -ErrorAction SilentlyContinue
        } while (-not $steamVrReady -and (Get-Date) -lt $deadline)
        if (-not $steamVrReady) {
            throw 'SteamVR did not become ready before the timeout.'
        }
    }
}

Set-StrictFollowVehicleCamera

$env:SteamAppId = '387990'
$env:SteamGameId = '387990'
$log = Join-Path $release 'ScrapNativeVR.log'
$logOffset = if (Test-Path -LiteralPath $log) { (Get-Item -LiteralPath $log).Length } else { 0 }
$process = Start-Process -FilePath $game -WorkingDirectory $release -PassThru
Write-Host "Scrap Mechanic VR started (PID $($process.Id))." -ForegroundColor Green

$deadline = (Get-Date).AddSeconds(35)
do {
    Start-Sleep -Seconds 1
    if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        throw 'Scrap Mechanic exited during VR initialization. Check ScrapNativeVR.log and ReShade.log.'
    }
    $ready = $false
    if (Test-Path -LiteralPath $log) {
        $bytes = [System.IO.File]::ReadAllBytes($log)
        if ($bytes.Length -lt $logOffset) { $logOffset = 0 }
        if ($bytes.Length -gt $logOffset) {
            $newLog = [System.Text.Encoding]::UTF8.GetString(
                $bytes,
                [int]$logOffset,
                [int]($bytes.Length - $logOffset))
            $ready = $newLog -match 'MILESTONE 3 NATIVE STEREO SUBMITTED'
        }
    }
} while (-not $ready -and (Get-Date) -lt $deadline)

if ($ready) {
    Write-Host 'Native stereo, head tracking, and Quest Touch input are active.' -ForegroundColor Green
} else {
    Write-Host 'The game is running, but VR focus was not confirmed. Put on the headset and check ScrapNativeVR.log.' -ForegroundColor Yellow
}
