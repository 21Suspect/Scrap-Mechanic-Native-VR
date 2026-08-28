[CmdletBinding()]
param(
    [ValidateSet('Install', 'ForceInstall', 'Verify', 'Repair', 'Uninstall', 'Start', 'ValidatePayload')]
    [string]$Action = 'Verify',
    [string]$GamePath,
    [string]$StateRoot = (Join-Path $env:LOCALAPPDATA 'ScrapMechanicVR-Chapter2'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$manifestPath = Join-Path $projectRoot 'manifest.json'
$payloadRoot = Join-Path $projectRoot 'payload'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToUpperInvariant()
}

function Get-StringSha256([string]$Value) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
}

function Get-Manifest {
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw "Patch manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.formatVersion -ne 1 -or
        $manifest.patchId -ne 'scrap-mechanic-native-vr-chapter2' -or
        -not $manifest.files) {
        throw 'Unsupported or incomplete patch manifest.'
    }
    return $manifest
}

function Test-OriginalHash($Entry, [string]$Hash) {
    foreach ($candidate in @($Entry.originalSha256)) {
        if ($candidate -and $candidate.ToUpperInvariant() -eq $Hash.ToUpperInvariant()) {
            return $true
        }
    }
    return $false
}

function Assert-Payload($Manifest) {
    $failures = @()
    foreach ($entry in $Manifest.files) {
        $source = Join-Path $payloadRoot $entry.path
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            $failures += "missing payload: $($entry.path)"
            continue
        }
        $actual = Get-Sha256 $source
        if ($actual -ne $entry.patchedSha256.ToUpperInvariant()) {
            $failures += "payload hash mismatch: $($entry.path) expected=$($entry.patchedSha256) actual=$actual"
        }
    }
    if ($failures.Count -gt 0) {
        throw ($failures -join [Environment]::NewLine)
    }
    Write-Host "Payload integrity verified: $($Manifest.files.Count) files." -ForegroundColor Green
}

function Add-GameCandidate([Collections.Generic.List[string]]$List, [string]$Candidate) {
    if ([string]::IsNullOrWhiteSpace($Candidate)) { return }
    try { $full = [IO.Path]::GetFullPath($Candidate) } catch { return }
    if (-not $List.Contains($full)) { $List.Add($full) }
}

function Find-GameRoot([string]$ExplicitPath, $Manifest) {
    $candidates = New-Object 'Collections.Generic.List[string]'
    Add-GameCandidate $candidates $ExplicitPath

    $cursor = $projectRoot
    for ($i = 0; $i -lt 4; $i++) {
        Add-GameCandidate $candidates $cursor
        $parent = Split-Path -Parent $cursor
        if (-not $parent -or $parent -eq $cursor) { break }
        $cursor = $parent
    }

    Add-GameCandidate $candidates 'C:\Program Files (x86)\Steam\steamapps\common\Scrap Mechanic'
    Add-GameCandidate $candidates 'C:\Program Files\Steam\steamapps\common\Scrap Mechanic'

    $steamRoots = @()
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        try {
            $value = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
            if ($value.SteamPath) { $steamRoots += $value.SteamPath }
            elseif ($value.InstallPath) { $steamRoots += $value.InstallPath }
        }
        catch {}
    }
    foreach ($steamRoot in $steamRoots | Select-Object -Unique) {
        Add-GameCandidate $candidates (Join-Path $steamRoot 'steamapps\common\Scrap Mechanic')
        $libraries = Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $libraries) {
            foreach ($line in Get-Content -LiteralPath $libraries) {
                if ($line -match '"path"\s+"([^"]+)"') {
                    $library = $matches[1].Replace('\\', '\')
                    Add-GameCandidate $candidates (Join-Path $library 'steamapps\common\Scrap Mechanic')
                }
            }
        }
    }

    foreach ($candidate in $candidates) {
        $exe = Join-Path $candidate $Manifest.game.executable
        if (Test-Path -LiteralPath $exe -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Could not find the Scrap Mechanic game data directory. Pass -GamePath with the folder containing Data, Survival, and Release.'
}

function Assert-GameBuild([string]$Root, $Manifest) {
    foreach ($folder in @('Data', 'Survival', 'Release')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $folder) -PathType Container)) {
            throw "Invalid game directory; missing $folder under $Root"
        }
    }
    $exe = Join-Path $Root $Manifest.game.executable
    $actual = Get-Sha256 $exe
    $expected = $Manifest.game.executableSha256.ToUpperInvariant()
    if ($actual -ne $expected) {
        throw "Unsupported ScrapMechanic.exe. This patch requires Steam build $($Manifest.game.buildId), executable SHA-256 $expected; found $actual."
    }

    $appManifest = Join-Path $Root '..\..\appmanifest_387990.acf'
    if (Test-Path -LiteralPath $appManifest) {
        $text = Get-Content -Raw -LiteralPath $appManifest
        if ($text -match '"buildid"\s+"(\d+)"' -and $matches[1] -ne [string]$Manifest.game.buildId) {
            throw "Steam reports build $($matches[1]); this patch requires build $($Manifest.game.buildId)."
        }
    }
    Write-Host "Supported game build verified: $($Manifest.game.buildId)." -ForegroundColor Green
}

function Assert-GameClosed {
    $running = Get-Process -Name ScrapMechanic -ErrorAction SilentlyContinue
    if ($running) {
        throw 'Scrap Mechanic is running. Close the game before installing or uninstalling.'
    }
}

function Clear-CompiledDataCache([string]$Root) {
    # Scrap Mechanic 1.0 does not invalidate this bundle when loose Lua files
    # are replaced. Leaving it in place makes the engine execute the previous
    # cached player callbacks, so the active-item bridge never reaches native
    # VR. This generated cache is rebuilt on the next launch.
    $cache = Join-Path $Root 'Cache\Bundle\core_data.cbo'
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        Remove-Item -LiteralPath $cache -Force
        if (Test-Path -LiteralPath $cache) {
            throw "Could not invalidate the compiled game-data cache: $cache"
        }
        Write-Host 'Invalidated Cache\Bundle\core_data.cbo; Scrap Mechanic will rebuild it on next launch.' -ForegroundColor Green
    }
}

function Get-NormalizedRoot([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    try {
        return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/').ToLowerInvariant()
    }
    catch {
        return $null
    }
}

function Get-StatePath([string]$Root) {
    $normalized = Get-NormalizedRoot $Root
    if (-not $normalized) { throw "Could not normalize game directory for install state: $Root" }
    $key = (Get-StringSha256 $normalized).Substring(0, 16)
    return Join-Path $StateRoot "install-state-$key.json"
}

function Write-JsonAtomic([string]$Path, $Value) {
    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $temporary = "$Path.tmp"
    $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Get-TargetStatus([string]$Root, $Entry) {
    $target = Join-Path $Root $Entry.path
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        return [pscustomobject]@{ Status = 'missing'; Hash = $null; Target = $target }
    }
    $hash = Get-Sha256 $target
    if ($Entry.patchedSha256 -and $hash -eq $Entry.patchedSha256.ToUpperInvariant()) {
        return [pscustomobject]@{ Status = 'patched'; Hash = $hash; Target = $target }
    }
    if (Test-OriginalHash $Entry $hash) {
        return [pscustomobject]@{ Status = 'original'; Hash = $hash; Target = $target }
    }
    return [pscustomobject]@{ Status = 'conflict'; Hash = $hash; Target = $target }
}

function Assert-NoLegacyVrFiles([string]$Root, $Manifest) {
    $blocked = @()
    foreach ($entry in @($Manifest.legacyFiles)) {
        $status = Get-TargetStatus $Root $entry
        if ($status.Status -ne 'missing' -and $status.Status -ne 'original') {
            $blocked += "$($entry.path) [$($status.Status) $($status.Hash)]"
        }
    }
    if ($blocked.Count -gt 0) {
        throw "Legacy or conflicting VR files were found. This Chapter 2 installer will not mix renderer generations. Use Repair / Clean Old VR Files, let Steam Verify finish if requested, then install again:`n$($blocked -join "`n")"
    }
}

function Install-Patch([string]$Root, $Manifest) {
    Assert-GameClosed
    Assert-NoLegacyVrFiles $Root $Manifest
    $statePath = Get-StatePath $Root
    if (Test-Path -LiteralPath $statePath) {
        Write-Host 'An existing managed installation was found. Restoring it safely before reinstalling.' -ForegroundColor Yellow
        Uninstall-Patch $Root $Manifest
    }

    $plan = @()
    $conflicts = @()
    foreach ($entry in $Manifest.files) {
        $status = Get-TargetStatus $Root $entry
        if ($status.Status -eq 'patched') {
            # Adopt exact pre-existing Chapter 2 files into managed state. They
            # are mod-owned, so a later uninstall should remove them.
            $plan += [pscustomobject]@{ Entry = $entry; Status = $status; Adopt = $true }
            continue
        }
        if ($status.Status -eq 'conflict') {
            if ($entry.runtimeMutable) {
                # ReShade and the VR INI may legitimately differ. Preserve the
                # current file, install the portable baseline, and restore the
                # user's previous file on uninstall.
                $plan += [pscustomobject]@{ Entry = $entry; Status = $status; Adopt = $false }
                continue
            }
            $conflicts += "$($entry.path) (unknown SHA-256 $($status.Hash))"
            continue
        }
        if ($status.Status -eq 'missing' -and -not $entry.originalMayBeMissing) {
            $conflicts += "$($entry.path) (required original is missing)"
            continue
        }
        $plan += [pscustomobject]@{ Entry = $entry; Status = $status; Adopt = $false }
    }
    if ($conflicts.Count -gt 0) {
        throw "Refusing to overwrite unsupported or separately modified files:`n$($conflicts -join "`n")"
    }
    if ($plan.Count -eq 0) {
        Write-Host 'Every target already matches this VR snapshot; nothing was changed.' -ForegroundColor Green
        return
    }

    $stamp = Get-Date -Format 'yyyyMMddTHHmmss'
    $key = (Get-StringSha256 $Root.ToLowerInvariant()).Substring(0, 16)
    $backupRoot = Join-Path $StateRoot "backups\$($Manifest.patchId)-$stamp-$key"
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    $changed = New-Object Collections.ArrayList

    try {
        foreach ($relativeDirectory in @($Manifest.directories)) {
            New-Item -ItemType Directory -Force -Path (Join-Path $Root $relativeDirectory) | Out-Null
        }
        foreach ($item in $plan) {
            $entry = $item.Entry
            $target = $item.Status.Target
            $source = Join-Path $payloadRoot $entry.path
            $record = [ordered]@{
                path = [string]$entry.path
                existed = [bool](-not $item.Adopt -and (Test-Path -LiteralPath $target -PathType Leaf))
                originalSha256 = $(if ($item.Adopt) { $null } else { $item.Status.Hash })
                installedSha256 = [string]$entry.patchedSha256
                backupRelativePath = $null
            }
            if ($item.Adopt) {
                [void]$changed.Add($record)
                Write-Host "Adopted existing exact file $($entry.path)"
                continue
            }
            if ($record.existed) {
                $backup = Join-Path $backupRoot $entry.path
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
                Copy-Item -LiteralPath $target -Destination $backup -Force
                if ((Get-Sha256 $backup) -ne $record.originalSha256) {
                    throw "Backup verification failed for $($entry.path)"
                }
                $record.backupRelativePath = [string]$entry.path
            }
            [void]$changed.Add($record)

            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            $temporary = "$target.scrapvr.tmp"
            Copy-Item -LiteralPath $source -Destination $temporary -Force
            if ((Get-Sha256 $temporary) -ne $entry.patchedSha256.ToUpperInvariant()) {
                throw "Staged payload verification failed for $($entry.path)"
            }
            Move-Item -LiteralPath $temporary -Destination $target -Force
            if ((Get-Sha256 $target) -ne $entry.patchedSha256.ToUpperInvariant()) {
                throw "Installed file verification failed for $($entry.path)"
            }
            Write-Host "Installed $($entry.path)"
        }

        Clear-CompiledDataCache $Root

        $state = [ordered]@{
            patchId = [string]$Manifest.patchId
            patchVersion = [string]$Manifest.patchVersion
            gameRoot = $Root
            gameExecutableSha256 = [string]$Manifest.game.executableSha256
            installedAt = (Get-Date).ToString('o')
            backupRoot = $backupRoot
            files = @($changed)
        }
        Write-JsonAtomic $statePath $state
        Write-Host "Installed and verified $($changed.Count) files." -ForegroundColor Green
        Write-Host "Restore state: $statePath" -ForegroundColor Green
        Write-Host "Backups: $backupRoot" -ForegroundColor Green
    }
    catch {
        Write-Warning "Install failed; rolling back $($changed.Count) touched files."
        for ($i = $changed.Count - 1; $i -ge 0; $i--) {
            $record = $changed[$i]
            $target = Join-Path $Root $record.path
            if ($record.existed) {
                $backup = Join-Path $backupRoot $record.backupRelativePath
                if (Test-Path -LiteralPath $backup) {
                    Copy-Item -LiteralPath $backup -Destination $target -Force
                }
            }
            else {
                Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            }
        }
        throw
    }
}

function Verify-Patch([string]$Root, $Manifest) {
    $patched = 0
    $mutable = 0
    $original = 0
    $missing = 0
    $conflict = 0
    foreach ($entry in $Manifest.files) {
        $status = Get-TargetStatus $Root $entry
        if ($entry.runtimeMutable -and $status.Status -eq 'conflict') {
            $mutable++
            Write-Host "MUTABLE  $($entry.path) [$($status.Hash)]" -ForegroundColor Cyan
            continue
        }
        switch ($status.Status) {
            'patched' { $patched++; Write-Host "PATCHED  $($entry.path)" -ForegroundColor Green }
            'original' { $original++; Write-Host "ORIGINAL $($entry.path)" -ForegroundColor Yellow }
            'missing' { $missing++; Write-Host "MISSING  $($entry.path)" -ForegroundColor Yellow }
            default { $conflict++; Write-Host "CONFLICT $($entry.path) [$($status.Hash)]" -ForegroundColor Red }
        }
    }
    Write-Host "Summary: patched=$patched mutable=$mutable original=$original missing=$missing conflict=$conflict"
    if (($patched + $mutable) -eq $Manifest.files.Count) {
        Write-Host 'The installed VR snapshot is complete; runtime-writable configuration is allowed to differ.' -ForegroundColor Green
        return
    }
    if ($conflict -gt 0) { throw 'Verification found conflicting files.' }
    throw 'The VR snapshot is not fully installed.'
}

function Repair-PatchTargets([string]$Root, $Manifest) {
    Assert-GameClosed
    $statePath = Get-StatePath $Root
    if (Test-Path -LiteralPath $statePath) {
        throw 'A patcher-managed installation exists. Use Uninstall / Restore instead so its exact backups are used.'
    }

    $stamp = Get-Date -Format 'yyyyMMddTHHmmss'
    $key = (Get-StringSha256 $Root.ToLowerInvariant()).Substring(0, 16)
    $quarantineRoot = Join-Path $StateRoot "repairs\$($Manifest.patchId)-$stamp-$key"
    $removed = New-Object Collections.ArrayList
    $needsSteam = New-Object Collections.ArrayList

    foreach ($entry in @($Manifest.files) + @($Manifest.legacyFiles)) {
        $status = Get-TargetStatus $Root $entry
        $remove = $false
        if ($entry.originalMayBeMissing) {
            $remove = $status.Status -ne 'missing'
        }
        elseif ($status.Status -eq 'patched' -or $status.Status -eq 'conflict') {
            $remove = $true
            [void]$needsSteam.Add([string]$entry.path)
        }
        elseif ($status.Status -eq 'missing') {
            [void]$needsSteam.Add([string]$entry.path)
        }

        if (-not $remove) { continue }
        $target = $status.Target
        $backup = Join-Path $quarantineRoot $entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
        Copy-Item -LiteralPath $target -Destination $backup -Force
        if ((Get-Sha256 $backup) -ne $status.Hash) {
            throw "Repair backup verification failed for $($entry.path)"
        }
        Remove-Item -LiteralPath $target -Force
        if (Test-Path -LiteralPath $target) {
            throw "Repair could not remove $($entry.path)"
        }
        [void]$removed.Add([ordered]@{
            path = [string]$entry.path
            sha256 = [string]$status.Hash
            previousStatus = [string]$status.Status
        })
        Write-Host "QUARANTINED $($entry.path) [$($status.Hash)]"
    }

    if ($removed.Count -gt 0) {
        $report = [ordered]@{
            patchId = [string]$Manifest.patchId
            gameRoot = $Root
            repairedAt = (Get-Date).ToString('o')
            files = @($removed)
        }
        Write-JsonAtomic (Join-Path $quarantineRoot 'repair-report.json') $report
        Write-Host "Repair backup: $quarantineRoot" -ForegroundColor Green
    }
    else {
        Write-Host 'No stale VR-managed files needed removal.' -ForegroundColor Green
    }

    Clear-CompiledDataCache $Root

    if ($needsSteam.Count -gt 0) {
        Write-Host "Steam must restore $($needsSteam.Count) original game file(s):" -ForegroundColor Yellow
        foreach ($path in $needsSteam) { Write-Host "  $path" -ForegroundColor Yellow }
    }
    Write-Host 'Repair cleanup completed. Run Steam Verify for Scrap Mechanic, wait for it to finish, then run Install again.' -ForegroundColor Green
}

function Uninstall-Patch([string]$Root, $Manifest, [switch]$IgnoreUnknownStateRecords) {
    Assert-GameClosed
    $statePath = Get-StatePath $Root
    if (-not (Test-Path -LiteralPath $statePath)) {
        throw "No patcher-managed install state exists for this game directory: $statePath"
    }
    $state = Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
    if (-not $state.files) {
        throw "Install state is incomplete and contains no managed file records: $statePath"
    }

    $entryByPath = @{}
    foreach ($entry in @($Manifest.files) + @($Manifest.legacyFiles)) {
        $entryByPath[[string]$entry.path] = $entry
    }

    # Do not trust stale metadata blindly, but do not reject an otherwise verifiable
    # state merely because Steam moved the library or an older patch id was stored.
    # Every record must map to this manifest and every pre-existing original/backup
    # hash must be one of the supported build's known originals.
    $stateIssues = New-Object Collections.ArrayList
    $recognizedRecords = New-Object Collections.ArrayList
    $unknownRecords = New-Object Collections.ArrayList
    if ($state.gameExecutableSha256 -and
        $state.gameExecutableSha256.ToUpperInvariant() -ne $Manifest.game.executableSha256.ToUpperInvariant()) {
        [void]$stateIssues.Add("state executable hash $($state.gameExecutableSha256) does not match supported build $($Manifest.game.executableSha256)")
    }
    foreach ($record in $state.files) {
        $recordPath = [string]$record.path
        $entry = $entryByPath[$recordPath]
        if (-not $entry) {
            [void]$unknownRecords.Add($record)
            if (-not $IgnoreUnknownStateRecords) {
                [void]$stateIssues.Add("unmanaged path in state: $recordPath")
            }
            continue
        }
        [void]$recognizedRecords.Add($record)
        if ($record.existed) {
            if (-not $record.originalSha256 -or
                (-not $entry.runtimeMutable -and
                 -not (Test-OriginalHash $entry ([string]$record.originalSha256)))) {
                [void]$stateIssues.Add("unsupported original hash for $recordPath")
            }
            if ([string]$record.backupRelativePath -ne $recordPath) {
                [void]$stateIssues.Add("unsafe backup path for $recordPath")
            }
        }
        elseif (-not $entry.originalMayBeMissing) {
            [void]$stateIssues.Add("state claims required original was absent: $recordPath")
        }
    }
    if ($stateIssues.Count -gt 0) {
        throw "Install state metadata cannot be adopted safely:`n$($stateIssues -join "`n")`nState file: $statePath"
    }

    if ($recognizedRecords.Count -eq 0) {
        throw "Install state contains no recognized VR-managed file records: $statePath"
    }

    if ($unknownRecords.Count -gt 0) {
        $stamp = Get-Date -Format 'yyyyMMddTHHmmss'
        $key = (Get-StringSha256 $Root.ToLowerInvariant()).Substring(0, 16)
        $recoveryRoot = Join-Path $StateRoot "recoveries\$($Manifest.patchId)-force-$stamp-$key"
        $stateCopy = Join-Path $recoveryRoot 'ignored-install-state.json'
        New-Item -ItemType Directory -Force -Path $recoveryRoot | Out-Null
        Copy-Item -LiteralPath $statePath -Destination $stateCopy -Force
        if ((Get-Sha256 $stateCopy) -ne (Get-Sha256 $statePath)) {
            throw 'Could not preserve the stale install state before force recovery.'
        }
        Write-Warning "Force recovery is ignoring $($unknownRecords.Count) unrecognized old state record(s). Their files will not be changed."
        foreach ($record in $unknownRecords) {
            Write-Host "IGNORED UNKNOWN $($record.path)" -ForegroundColor Yellow
        }
        Write-Host "Preserved old state metadata: $stateCopy" -ForegroundColor Green
    }

    $storedRoot = Get-NormalizedRoot ([string]$state.gameRoot)
    $currentRoot = Get-NormalizedRoot $Root
    if ($storedRoot -ne $currentRoot) {
        Write-Warning "Stored game directory differs from the current verified Steam directory. Stored='$($state.gameRoot)' Current='$Root'. Validated managed records will be restored into the current directory."
    }
    if ([string]$state.patchId -ne [string]$Manifest.patchId) {
        Write-Warning "Stored patch id '$($state.patchId)' differs from '$($Manifest.patchId)'. The state records are compatible with this manifest and will be adopted."
    }

    # A copied or manually cleaned LocalAppData folder can leave a valid state file
    # whose timestamped backup directory is gone. Record backup health per file so
    # exact originals already restored by Steam can be accepted safely instead of
    # trapping the user behind a generic exit code.
    $backupStatus = @{}
    $invalidBackups = New-Object Collections.ArrayList
    foreach ($record in $recognizedRecords) {
        if (-not $record.existed) { continue }
        $backup = $null
        $valid = $false
        $reason = 'backup root is missing'
        if ($state.backupRoot -and $record.backupRelativePath -and
            (Test-Path -LiteralPath $state.backupRoot -PathType Container)) {
            $backup = Join-Path $state.backupRoot $record.backupRelativePath
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                $reason = 'backup file is missing'
            }
            else {
                $backupHash = Get-Sha256 $backup
                if ($backupHash -eq $record.originalSha256.ToUpperInvariant()) {
                    $valid = $true
                    $reason = $null
                }
                else {
                    $reason = "backup hash mismatch ($backupHash)"
                }
            }
        }
        $backupStatus[[string]$record.path] = [pscustomobject]@{
            Valid = $valid
            Path = $backup
            Reason = $reason
        }
        if (-not $valid) {
            [void]$invalidBackups.Add([string]$record.path)
            Write-Warning "Backup unavailable for $($record.path): $reason. The restorer will accept an exact known original already present in the game."
        }
    }

    $stamp = Get-Date -Format 'yyyyMMddTHHmmss'
    $key = (Get-StringSha256 $Root.ToLowerInvariant()).Substring(0, 16)
    $quarantineRoot = Join-Path $StateRoot "conflicts\$($Manifest.patchId)-$stamp-$key"
    $preserved = New-Object Collections.ArrayList
    foreach ($record in $recognizedRecords) {
        $target = Join-Path $Root $record.path
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { continue }
        $actual = Get-Sha256 $target
        $isInstalled = $actual -eq $record.installedSha256.ToUpperInvariant()
        $entry = $entryByPath[[string]$record.path]
        $isOriginal = $record.existed -and
            ($actual -eq $record.originalSha256.ToUpperInvariant() -or
             ($entry -and (Test-OriginalHash $entry $actual)))
        $backupInfo = $backupStatus[[string]$record.path]
        $hasRestorableBackup = -not $record.existed -or ($backupInfo -and $backupInfo.Valid)
        if ($isOriginal -or ($isInstalled -and $hasRestorableBackup)) { continue }

        $preservedPath = Join-Path $quarantineRoot $record.path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $preservedPath) | Out-Null
        Copy-Item -LiteralPath $target -Destination $preservedPath -Force
        if ((Get-Sha256 $preservedPath) -ne $actual) {
            throw "Could not preserve the modified managed file: $($record.path)"
        }
        [void]$preserved.Add([ordered]@{
            path = [string]$record.path
            sha256 = [string]$actual
            preservedRelativePath = [string]$record.path
        })
        Write-Host "PRESERVED modified $($record.path) [$actual]" -ForegroundColor Yellow
    }

    if ($preserved.Count -gt 0) {
        $report = [ordered]@{
            patchId = [string]$Manifest.patchId
            patchVersion = [string]$state.patchVersion
            gameRoot = $Root
            preservedAt = (Get-Date).ToString('o')
            files = @($preserved)
        }
        Write-JsonAtomic (Join-Path $quarantineRoot 'conflict-report.json') $report
        Write-Host "Modified managed files were preserved at $quarantineRoot" -ForegroundColor Green
    }

    $needsSteam = New-Object Collections.ArrayList
    foreach ($record in @($recognizedRecords)) {
        $target = Join-Path $Root $record.path
        if ($record.existed) {
            $backupInfo = $backupStatus[[string]$record.path]
            if ($backupInfo -and $backupInfo.Valid) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item -LiteralPath $backupInfo.Path -Destination $target -Force
                if ((Get-Sha256 $target) -ne $record.originalSha256.ToUpperInvariant()) {
                    throw "Restore verification failed: $($record.path)"
                }
                Write-Host "Restored $($record.path)"
                continue
            }

            $entry = $entryByPath[[string]$record.path]
            $alreadyOriginal = $false
            if (Test-Path -LiteralPath $target -PathType Leaf) {
                $actual = Get-Sha256 $target
                $alreadyOriginal = $actual -eq $record.originalSha256.ToUpperInvariant() -or
                    ($entry -and (Test-OriginalHash $entry $actual))
            }
            if ($alreadyOriginal) {
                Write-Host "ALREADY ORIGINAL $($record.path) (missing backup was not needed)" -ForegroundColor Green
                continue
            }

            # Never guess at an original. Remove only the manifest-managed target,
            # after the preservation pass above, so Steam can restore the exact file.
            if (Test-Path -LiteralPath $target -PathType Leaf) {
                Remove-Item -LiteralPath $target -Force
            }
            [void]$needsSteam.Add([string]$record.path)
            Write-Host "NEEDS STEAM $($record.path)" -ForegroundColor Yellow
        }
        else {
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            Write-Host "Removed $($record.path)"
        }
    }
    Clear-CompiledDataCache $Root
    Remove-Item -LiteralPath $statePath -Force
    foreach ($relativeDirectory in @($Manifest.directories) | Sort-Object Length -Descending) {
        $directory = Join-Path $Root $relativeDirectory
        if ((Test-Path -LiteralPath $directory -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $directory -Force -ErrorAction SilentlyContinue)) {
            Remove-Item -LiteralPath $directory -Force
        }
    }
    if ($needsSteam.Count -gt 0) {
        $recoveryRoot = Join-Path $StateRoot "recoveries\$($Manifest.patchId)-$stamp-$key"
        $report = [ordered]@{
            patchId = [string]$Manifest.patchId
            patchVersion = [string]$state.patchVersion
            gameRoot = $Root
            recoveredAt = (Get-Date).ToString('o')
            missingBackupFiles = @($invalidBackups)
            steamRestoreRequired = @($needsSteam)
        }
        Write-JsonAtomic (Join-Path $recoveryRoot 'recovery-report.json') $report
        Write-Host "STEAM_REPAIR_REQUIRED: $($needsSteam.Count) original game file(s) could not be restored because their backups were unavailable." -ForegroundColor Yellow
        foreach ($path in $needsSteam) { Write-Host "  $path" -ForegroundColor Yellow }
        Write-Host "Recovery report: $recoveryRoot\recovery-report.json" -ForegroundColor Yellow
        throw 'The VR files and stale install state were removed, but Steam must verify Scrap Mechanic before the VR mod can be reinstalled.'
    }

    if ($invalidBackups.Count -gt 0) {
        Write-Host "Recovered stale install state safely: $($invalidBackups.Count) missing backup(s) were unnecessary because exact original files were already present." -ForegroundColor Green
    }
    Write-Host 'Uninstall completed and every required original file was hash-verified.' -ForegroundColor Green
    if ($state.backupRoot -and (Test-Path -LiteralPath $state.backupRoot -PathType Container)) {
        Write-Host "The timestamped backup remains at $($state.backupRoot)." -ForegroundColor Green
    }
}

function Force-InstallPatch([string]$Root, $Manifest) {
    Assert-GameClosed
    $statePath = Get-StatePath $Root
    if (Test-Path -LiteralPath $statePath) {
        Write-Host 'Force recovery: restoring all recognized current and legacy VR records first.' -ForegroundColor Yellow
        Uninstall-Patch $Root $Manifest -IgnoreUnknownStateRecords
    }
    else {
        Write-Host 'No managed install state exists; running guarded cleanup of current VR targets.' -ForegroundColor Yellow
        Repair-PatchTargets $Root $Manifest
        foreach ($entry in @($Manifest.files) + @($Manifest.legacyFiles)) {
            $status = Get-TargetStatus $Root $entry
            if ((-not $entry.originalMayBeMissing -and $status.Status -ne 'original') -or
                ($entry.originalMayBeMissing -and $status.Status -ne 'missing')) {
                Write-Host 'STEAM_REPAIR_REQUIRED: Guarded cleanup could not reconstruct every required original file.' -ForegroundColor Yellow
                throw 'Steam must verify Scrap Mechanic before Force Reset / Reinstall can continue.'
            }
        }
    }
    Install-Patch $Root $Manifest
    Write-Host 'Force Reset / Reinstall completed.' -ForegroundColor Green
}

$manifest = Get-Manifest
Assert-Payload $manifest
if ($Action -eq 'ValidatePayload') { return }

$root = Find-GameRoot $GamePath $manifest
Write-Host "Game directory: $root"
Assert-GameBuild $root $manifest

switch ($Action) {
    'Install' { Install-Patch $root $manifest }
    'ForceInstall' { Force-InstallPatch $root $manifest }
    'Verify' { Verify-Patch $root $manifest }
    'Repair' { Repair-PatchTargets $root $manifest }
    'Uninstall' { Uninstall-Patch $root $manifest }
    'Start' {
        Start-Process 'steam://rungameid/387990'
    }
}
