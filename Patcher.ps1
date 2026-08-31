[CmdletBinding()]
param(
    [ValidateSet('Install', 'ForceInstall', 'Verify', 'Repair', 'Uninstall', 'Start', 'ValidatePayload')]
    [string]$Action = 'Verify',
    [string]$GamePath,
    [string]$StateRoot = (Join-Path $env:LOCALAPPDATA 'ScrapMechanicVR-Chapter2'),
    [switch]$Force,
    [string[]]$AdditionalSteamRoot = @()
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

function Test-HistoricalPatchedHash($Entry, [string]$Hash) {
    if ([string]::IsNullOrWhiteSpace($Hash)) { return $false }
    foreach ($candidate in @($Entry.historicalPatchedSha256)) {
        if ($candidate -and $candidate.ToUpperInvariant() -eq $Hash.ToUpperInvariant()) {
            return $true
        }
    }
    return $false
}

function Test-ManagedPatchedHash($Entry, [string]$Hash) {
    if ([string]::IsNullOrWhiteSpace($Hash)) { return $false }
    if ($Entry.patchedSha256 -and
        $Entry.patchedSha256.ToUpperInvariant() -eq $Hash.ToUpperInvariant()) {
        return $true
    }
    return Test-HistoricalPatchedHash $Entry $Hash
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

    # This is a release-blocking gameplay contract, not just a collection of
    # payload hashes. A hash refresh must never be able to bless weapon scripts
    # which have accidentally reverted to Scrap Mechanic's desktop crosshair.
    $chapter2BridgePath = Join-Path $payloadRoot 'Survival\Scripts\game\Chapter2VR.lua'
    if (Test-Path -LiteralPath $chapter2BridgePath -PathType Leaf) {
        $chapter2Bridge = [IO.File]::ReadAllText($chapter2BridgePath)
        foreach ($requiredMarker in @(
            'ScrapVRProjectilePoseNative',
            'tracked_barrel_native_logic_task',
            'source = firePos and "vr_barrel"',
            'authoritative and "blocked" or "pc_fallback"'
        )) {
            if (-not $chapter2Bridge.Contains($requiredMarker)) {
                $failures += "VR barrel regression: Chapter2VR.lua is missing '$requiredMarker'"
            }
        }
    }

    $gunScripts = @(
        'ClayRifle.lua',
        'PotatoRifle.lua',
        'PotatoShotgun.lua',
        'PotatoGatling.lua',
        'PotatoLauncher.lua',
        'ScrapPotatoRifle.lua'
    )
    foreach ($gunScript in $gunScripts) {
        $relativePath = "Survival\Scripts\game\tools\$gunScript"
        $gunPath = Join-Path $payloadRoot $relativePath
        if (-not (Test-Path -LiteralPath $gunPath -PathType Leaf)) {
            $failures += "VR barrel regression: missing gun script $relativePath"
            continue
        }
        $gunText = [IO.File]::ReadAllText($gunPath)
        foreach ($requiredMarker in @(
            'Chapter2VR.gunFirePose( self.tool, true )',
            'if vrAuthoritative and not vrFirePos then return end',
            'vrGunAim and vrDirection or sm.localPlayer.getDirection()',
            'vrGunAim and vrFirePos'
        )) {
            if (-not $gunText.Contains($requiredMarker)) {
                $failures += "VR barrel regression: $gunScript is missing '$requiredMarker'"
            }
        }
    }

    $nativeAddonPath = Join-Path $payloadRoot 'Release\smvr_native_vr_v1.addon64'
    if (Test-Path -LiteralPath $nativeAddonPath -PathType Leaf) {
        $nativeAddonStrings = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($nativeAddonPath))
        if (-not $nativeAddonStrings.Contains('ScrapVRProjectilePoseNative')) {
            $failures += 'VR barrel regression: native addon does not export the direct Logic Task projectile-pose bridge'
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
    $pathRoot = [IO.Path]::GetPathRoot($full)
    if ([string]::IsNullOrWhiteSpace($pathRoot) -or -not [IO.Directory]::Exists($pathRoot)) { return }
    if (-not $List.Contains($full)) { $List.Add($full) }
}

function Join-FileSystemPath([string]$BasePath, [string]$ChildPath) {
    if ([string]::IsNullOrWhiteSpace($BasePath) -or [string]::IsNullOrWhiteSpace($ChildPath)) {
        return $null
    }
    try {
        return [IO.Path]::Combine($BasePath, $ChildPath)
    }
    catch {
        return $null
    }
}

function Find-GameRoot([string]$ExplicitPath, $Manifest, [string[]]$ExtraSteamRoots = @()) {
    $candidates = New-Object 'Collections.Generic.List[string]'
    Add-GameCandidate $candidates $ExplicitPath

    # The installer already supplies its validated game directory. Prefer it
    # immediately so unrelated stale Steam library records cannot break an
    # install, repair, or uninstall operation.
    if ($candidates.Count -gt 0) {
        $explicitExe = Join-FileSystemPath $candidates[0] ([string]$Manifest.game.executable)
        if ($explicitExe -and [IO.File]::Exists($explicitExe)) {
            return $candidates[0]
        }
    }

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
    $steamRoots += @($ExtraSteamRoots)
    foreach ($steamRoot in $steamRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique) {
        Add-GameCandidate $candidates (Join-FileSystemPath $steamRoot 'steamapps\common\Scrap Mechanic')
        $libraries = Join-FileSystemPath $steamRoot 'steamapps\libraryfolders.vdf'
        if ($libraries -and [IO.File]::Exists($libraries)) {
            foreach ($line in Get-Content -LiteralPath $libraries) {
                if ($line -match '"path"\s+"([^"]+)"') {
                    $library = $matches[1].Replace('\\', '\')
                    Add-GameCandidate $candidates (Join-FileSystemPath $library 'steamapps\common\Scrap Mechanic')
                }
            }
        }
    }

    foreach ($candidate in $candidates) {
        $exe = Join-FileSystemPath $candidate ([string]$Manifest.game.executable)
        if ($exe -and [IO.File]::Exists($exe)) {
            return [IO.Path]::GetFullPath($candidate)
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
    $helperNames = @(
        'ScrapMechanicVR-ClayCalibration',
        'ScrapMechanicVR-HeldCalibration'
    )
    $runningHelpers = @(Get-Process -Name $helperNames -ErrorAction SilentlyContinue)
    if ($runningHelpers.Count -gt 0) {
        $names = @($runningHelpers | Select-Object -ExpandProperty ProcessName -Unique)
        throw "A Scrap Mechanic VR calibration helper is running. Close it before installing or uninstalling: $($names -join ', ')"
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

function Test-ManifestIncludesCompiledDataCache($Manifest) {
    foreach ($entry in @($Manifest.files)) {
        if ([string]$entry.path -ieq 'Cache\Bundle\core_data.cbo') {
            return $true
        }
    }
    return $false
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

function Get-ContainedPath([string]$BasePath, [string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($BasePath) -or
        [string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath)) {
        return $null
    }
    try {
        $base = [IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $full = [IO.Path]::GetFullPath([IO.Path]::Combine($base, $RelativePath))
        if (-not $full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
            return $null
        }
        return $full
    }
    catch {
        return $null
    }
}

function Get-CompatiblePackageManifest($State, $CurrentManifest) {
    $version = [string]$State.patchVersion
    if ([string]::IsNullOrWhiteSpace($version)) { return $null }

    $packagesRoot = Join-Path $StateRoot 'packages'
    $packageRoot = Get-ContainedPath $packagesRoot $version
    if (-not $packageRoot) { return $null }
    $candidatePath = Join-Path $packageRoot 'manifest.json'
    if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) { return $null }

    try {
        $candidate = Get-Content -Raw -LiteralPath $candidatePath | ConvertFrom-Json
    }
    catch {
        Write-Warning "Could not read previous package metadata: $candidatePath"
        return $null
    }
    if ($candidate.formatVersion -ne 1 -or
        [string]$candidate.patchId -ne [string]$CurrentManifest.patchId -or
        [string]$candidate.game.executableSha256 -ne [string]$CurrentManifest.game.executableSha256 -or
        -not $candidate.files) {
        Write-Warning "Previous package metadata is incompatible and will not be trusted: $candidatePath"
        return $null
    }
    Write-Host "Loaded verified removal metadata for installed version $version." -ForegroundColor Green
    return $candidate
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
    if (Test-HistoricalPatchedHash $Entry $hash) {
        return [pscustomobject]@{ Status = 'historical'; Hash = $hash; Target = $target }
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
        throw "Legacy or conflicting VR files were found. This Chapter 2 installer will not mix renderer generations. Click Uninstall VR Mod to preserve and remove recognized VR files, let Steam verification finish if it opens, then click Install VR Mod again:`n$($blocked -join "`n")"
    }
}

function Remove-ExactLegacyVrFiles([string]$Root, $Manifest) {
    $matches = @()
    foreach ($entry in @($Manifest.legacyFiles)) {
        $status = Get-TargetStatus $Root $entry
        if ($status.Status -eq 'patched' -or $status.Status -eq 'historical') {
            $matches += [pscustomobject]@{ Entry = $entry; Status = $status }
        }
    }
    if ($matches.Count -eq 0) { return }

    $stamp = Get-Date -Format 'yyyyMMddTHHmmss'
    $key = (Get-StringSha256 $Root.ToLowerInvariant()).Substring(0, 16)
    $quarantineRoot = Join-Path $StateRoot "legacy-removals\$($Manifest.patchId)-$stamp-$key"
    Write-Host "Detected $($matches.Count) exact file(s) from the legacy VR build. Preserving and removing them before installation." -ForegroundColor Yellow
    foreach ($match in $matches) {
        $backup = Join-Path $quarantineRoot $match.Entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
        Copy-Item -LiteralPath $match.Status.Target -Destination $backup -Force
        if ((Get-Sha256 $backup) -ne $match.Status.Hash) {
            throw "Legacy-file preservation failed for $($match.Entry.path)"
        }
        Remove-Item -LiteralPath $match.Status.Target -Force
        if (Test-Path -LiteralPath $match.Status.Target) {
            throw "Could not remove legacy VR file $($match.Entry.path)"
        }
        Write-Host "Removed legacy VR file $($match.Entry.path)"
    }
    Write-Host "Legacy files were preserved at $quarantineRoot" -ForegroundColor Green
}

function Install-Patch([string]$Root, $Manifest) {
    Assert-GameClosed
    $statePath = Get-StatePath $Root
    if (Test-Path -LiteralPath $statePath) {
        $existingState = Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
        Write-Host "Existing managed VR version '$($existingState.patchVersion)' detected." -ForegroundColor Yellow
        Write-Host 'The previous version will now be restored and removed using its verified backup metadata before this version is installed.' -ForegroundColor Yellow
        Uninstall-Patch $Root $Manifest -ForUpgrade
    }
    Remove-ExactLegacyVrFiles $Root $Manifest
    Assert-NoLegacyVrFiles $Root $Manifest

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
        if ($status.Status -eq 'historical') {
            # This is an exact, path-specific payload from one of our previous
            # releases. Replace it without adopting it as a game original.
            $plan += [pscustomobject]@{ Entry = $entry; Status = $status; Adopt = $false; ReplaceHistorical = $true }
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
        $plan += [pscustomobject]@{ Entry = $entry; Status = $status; Adopt = $false; ReplaceHistorical = $false }
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
                existed = [bool](-not $item.Adopt -and -not $item.ReplaceHistorical -and (Test-Path -LiteralPath $target -PathType Leaf))
                originalSha256 = $(if ($item.Adopt -or $item.ReplaceHistorical) { $null } else { $item.Status.Hash })
                installedSha256 = [string]$entry.patchedSha256
                backupRelativePath = $null
                replacedManagedSha256 = $(if ($item.ReplaceHistorical) { [string]$item.Status.Hash } else { $null })
                rollbackRelativePath = $null
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
            elseif ($item.ReplaceHistorical) {
                $rollback = Join-Path $backupRoot ("managed-migration\\" + [string]$entry.path)
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $rollback) | Out-Null
                Copy-Item -LiteralPath $target -Destination $rollback -Force
                if ((Get-Sha256 $rollback) -ne $record.replacedManagedSha256) {
                    throw "Previous-version preservation failed for $($entry.path)"
                }
                $record.rollbackRelativePath = "managed-migration\\$($entry.path)"
                Write-Host "Migrating recognized previous VR file $($entry.path) [$($record.replacedManagedSha256)]" -ForegroundColor Yellow
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

        if (Test-ManifestIncludesCompiledDataCache $Manifest) {
            Write-Host 'Installed verified Cache\Bundle\core_data.cbo seed; the game may update this runtime cache normally.' -ForegroundColor Green
        }
        else {
            Clear-CompiledDataCache $Root
        }

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
        Write-Host 'Running automatic post-install verification...' -ForegroundColor Cyan
        Verify-Patch $Root $Manifest
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
            elseif ($record.rollbackRelativePath) {
                $rollback = Join-Path $backupRoot $record.rollbackRelativePath
                if (Test-Path -LiteralPath $rollback -PathType Leaf) {
                    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                    Copy-Item -LiteralPath $rollback -Destination $target -Force
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
        elseif ($status.Status -eq 'patched' -or $status.Status -eq 'historical' -or $status.Status -eq 'conflict') {
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

function Uninstall-Patch([string]$Root, $Manifest, [switch]$IgnoreUnknownStateRecords, [switch]$ForUpgrade) {
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
    $currentEntryByPath = @{}
    $previousEntryByPath = @{}
    foreach ($entry in @($Manifest.files) + @($Manifest.legacyFiles)) {
        $entryPath = [string]$entry.path
        $entryByPath[$entryPath] = $entry
        $currentEntryByPath[$entryPath] = $entry
    }
    $previousManifest = Get-CompatiblePackageManifest $state $Manifest
    if ($previousManifest) {
        foreach ($entry in @($previousManifest.files) + @($previousManifest.legacyFiles)) {
            $entryPath = [string]$entry.path
            if ([string]::IsNullOrWhiteSpace($entryPath)) { continue }
            $contained = Get-ContainedPath $Root $entryPath
            if (-not $contained) {
                throw "Previous package metadata contains an unsafe managed path: $entryPath"
            }
            $previousEntryByPath[$entryPath] = $entry
            if (-not $entryByPath.ContainsKey($entryPath)) {
                $entryByPath[$entryPath] = $entry
            }
        }
    }

    # Do not trust stale metadata blindly, but do not reject an otherwise verifiable
    # state merely because Steam moved the library or an older patch id was stored.
    # Every record must map to this manifest and every pre-existing original/backup
    # hash must be one of the supported build's known originals.
    $stateIssues = New-Object Collections.ArrayList
    $recognizedRecords = New-Object Collections.ArrayList
    $unknownRecords = New-Object Collections.ArrayList
    $historicalOriginalPaths = @{}
    if ($state.backupRoot) {
        try {
            $allowedBackupRoot = [IO.Path]::GetFullPath((Join-Path $StateRoot 'backups')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
            $storedBackupRoot = [IO.Path]::GetFullPath([string]$state.backupRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
            if (-not $storedBackupRoot.StartsWith($allowedBackupRoot, [StringComparison]::OrdinalIgnoreCase)) {
                [void]$stateIssues.Add("backup root is outside the installer backup directory: $($state.backupRoot)")
            }
        }
        catch {
            [void]$stateIssues.Add("backup root is invalid: $($state.backupRoot)")
        }
    }
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
        if (-not (Get-ContainedPath $Root $recordPath)) {
            [void]$stateIssues.Add("unsafe managed path in state: $recordPath")
            continue
        }
        [void]$recognizedRecords.Add($record)
        $previousEntry = $previousEntryByPath[$recordPath]
        if ($previousEntry -and $record.installedSha256 -and
            [string]$previousEntry.patchedSha256 -ne [string]$record.installedSha256) {
            Write-Warning "The extracted package for '$($state.patchVersion)' differs from the recorded installed hash for $recordPath. The state record and its hash-verified backup will be used instead."
        }
        if ($record.existed) {
            if (-not $record.originalSha256) {
                [void]$stateIssues.Add("missing original hash for $recordPath")
            }
            elseif (Test-HistoricalPatchedHash $currentEntryByPath[$recordPath] ([string]$record.originalSha256)) {
                $historicalOriginalPaths[$recordPath] = $true
                Write-Warning "The backup recorded as the original for $recordPath is actually a recognized older VR payload. It will not be restored as a game original."
            }
            if ([string]$record.backupRelativePath -ne $recordPath) {
                [void]$stateIssues.Add("unsafe backup path for $recordPath")
            }
        }
        elseif (-not $entry.originalMayBeMissing -and -not $record.replacedManagedSha256) {
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
            -not $historicalOriginalPaths.ContainsKey([string]$record.path) -and
            ($actual -eq $record.originalSha256.ToUpperInvariant() -or
             ($entry -and (Test-OriginalHash $entry $actual)))
        $isKnownManaged = $entry -and (Test-ManagedPatchedHash $entry $actual)
        $backupInfo = $backupStatus[[string]$record.path]
        $hasRestorableBackup = -not $record.existed -or ($backupInfo -and $backupInfo.Valid)
        if ($isOriginal -or $isKnownManaged -or ($isInstalled -and $hasRestorableBackup)) { continue }

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
            if ($historicalOriginalPaths.ContainsKey([string]$record.path)) {
                if ($ForUpgrade) {
                    Write-Host "Leaving recognized previous VR payload in place for direct migration: $($record.path)" -ForegroundColor Yellow
                    continue
                }
                if (Test-Path -LiteralPath $target -PathType Leaf) {
                    Remove-Item -LiteralPath $target -Force
                }
                $entry = $entryByPath[[string]$record.path]
                if (-not $entry.originalMayBeMissing) {
                    [void]$needsSteam.Add([string]$record.path)
                    Write-Host "NEEDS STEAM $($record.path) (an older VR payload had been recorded as its original)" -ForegroundColor Yellow
                }
                else {
                    Write-Host "Removed nested previous VR payload $($record.path)"
                }
                continue
            }
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
            if (Test-Path -LiteralPath $target -PathType Leaf) {
                throw "Could not remove $($record.path). Close any VR calibration helper or other program using this file, then try again."
            }
            Write-Host "Removed $($record.path)"
        }
    }
    $stateManagedCompiledCache = @($recognizedRecords | Where-Object {
        [string]$_.path -ieq 'Cache\Bundle\core_data.cbo'
    }).Count -gt 0
    if (-not $stateManagedCompiledCache) {
        # Older managed states predate the cache seed. Remove their stale
        # compiled bundle during migration before the new seed is installed.
        Clear-CompiledDataCache $Root
    }
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

function Verify-UninstalledPatch([string]$Root, $Manifest) {
    $failures = @()
    foreach ($entry in @($Manifest.files) + @($Manifest.legacyFiles)) {
        $status = Get-TargetStatus $Root $entry
        if ($entry.originalMayBeMissing) {
            if ($status.Status -eq 'patched' -or $status.Status -eq 'historical') {
                $failures += "$($entry.path) still matches a VR-mod payload"
            }
            continue
        }
        if ($status.Status -ne 'original') {
            $failures += "$($entry.path) is $($status.Status) instead of the supported game original"
        }
    }
    if ($failures.Count -gt 0) {
        Write-Host 'STEAM_REPAIR_REQUIRED: automatic post-uninstall verification found game originals that Steam must restore.' -ForegroundColor Yellow
        foreach ($failure in $failures) { Write-Host "  $failure" -ForegroundColor Yellow }
        throw 'The VR mod was removed, but Steam must verify Scrap Mechanic before the game is complete.'
    }
    Write-Host 'Automatic post-uninstall verification passed: no recognized VR payload remains and all required game originals are restored.' -ForegroundColor Green
}

function Uninstall-AnyPatch([string]$Root, $Manifest) {
    Assert-GameClosed
    $statePath = Get-StatePath $Root
    if (Test-Path -LiteralPath $statePath) {
        $state = Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
        Write-Host "Managed VR version '$($state.patchVersion)' detected. Its verified state and backups will be used for removal." -ForegroundColor Yellow
        Uninstall-Patch $Root $Manifest
        Verify-UninstalledPatch $Root $Manifest
        return
    }

    Write-Host 'No managed install state exists. Scanning current and recognized legacy VR paths before guarded cleanup.' -ForegroundColor Yellow
    Repair-PatchTargets $Root $Manifest
    Verify-UninstalledPatch $Root $Manifest
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

$root = Find-GameRoot $GamePath $manifest $AdditionalSteamRoot
Write-Host "Game directory: $root"
Assert-GameBuild $root $manifest

switch ($Action) {
    'Install' { Install-Patch $root $manifest }
    'ForceInstall' { Force-InstallPatch $root $manifest }
    'Verify' { Verify-Patch $root $manifest }
    'Repair' { Repair-PatchTargets $root $manifest }
    'Uninstall' { Uninstall-AnyPatch $root $manifest }
    'Start' {
        Start-Process 'steam://rungameid/387990'
    }
}
