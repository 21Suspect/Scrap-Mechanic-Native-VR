$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('smvr-upgrade-test-' + [Guid]::NewGuid().ToString('N'))
$testState = Join-Path $testRoot 'state'
. (Join-Path $repo 'Patcher.ps1') -Action ValidatePayload -StateRoot $testState

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

foreach ($scenario in @('updated-original', 'same-build', 'missing-new-original')) {
    $game = Join-Path $testRoot $scenario
    $backup = Join-Path $testState ('backups\' + $scenario)
    New-Item -ItemType Directory -Force -Path (Join-Path $game 'Data'), (Join-Path $game 'Release'), (Join-Path $backup 'Data') | Out-Null
    $target = Join-Path $game 'Data\test.lua'
    $saved = Join-Path $backup 'Data\test.lua'
    [IO.File]::WriteAllText((Join-Path $game 'Release\ScrapMechanic.exe'), 'new-game-executable-fixture')
    $exeHash = Get-Sha256 (Join-Path $game 'Release\ScrapMechanic.exe')
    [IO.File]::WriteAllText($saved, 'old-game-original')
    $oldHash = Get-Sha256 $saved
    [IO.File]::WriteAllText($target, 'new-game-original')
    $newHash = Get-Sha256 $target
    [IO.File]::WriteAllText($target, 'old-vr-payload')
    $patchedHash = Get-Sha256 $target
    if ($scenario -eq 'updated-original') { [IO.File]::WriteAllText($target, 'new-game-original') }
    $before = Get-Sha256 $target
    $cache = Join-Path $game 'Cache\Bundle\core_data.cbo'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $cache) | Out-Null
    [IO.File]::WriteAllText($cache, 'runtime-cache-from-older-install')
    $sameBuild = $scenario -eq 'same-build'
    $fixtureManifest = [pscustomobject]@{
        patchId = 'scrap-mechanic-native-vr-chapter2'; patchVersion = 'test-new'
        game = [pscustomobject]@{ executable = 'Release\ScrapMechanic.exe'; executableSha256 = $exeHash }
        files = @([pscustomobject]@{ path = 'Data\test.lua'; patchedSha256 = $patchedHash;
            originalSha256 = @($(if ($sameBuild) { $oldHash } else { $newHash })) })
        legacyFiles = @(); directories = @(); compiledDataCachePolicy = 'rebuild'
    }
    $fixtureState = [ordered]@{
        patchId = $fixtureManifest.patchId; patchVersion = 'test-old'; gameRoot = $game; backupRoot = $backup
        gameExecutableSha256 = $(if ($sameBuild) { $exeHash } else { 'A' * 64 })
        files = @([ordered]@{ path = 'Data\test.lua'; existed = $true; originalSha256 = $oldHash;
            installedSha256 = $patchedHash; backupRelativePath = 'Data\test.lua' },
            [ordered]@{ path = 'Cache\Bundle\core_data.cbo'; existed = $false;
            installedSha256 = (Get-Sha256 $cache) })
    }
    $stateFile = Get-StatePath $game
    Write-JsonAtomic $stateFile $fixtureState
    if ($scenario -eq 'missing-new-original') {
        $rejected = $false
        try { Uninstall-Patch $game $fixtureManifest -ForUpgrade }
        catch {
            if ($_.Exception.Message -notlike 'Steam must verify the updated game files*') { throw }
            $rejected = $true
        }
        Assert-Test $rejected 'Upgrade accepted a stale original for the new build'
        Assert-Test ((Get-Sha256 $target) -eq $before) 'Rejected upgrade modified the current file'
        Assert-Test (Test-Path -LiteralPath $stateFile) 'Rejected upgrade removed recovery state'
        Assert-Test (Test-Path -LiteralPath $cache) 'Rejected upgrade removed the existing cache'
    }
    else {
        Uninstall-Patch $game $fixtureManifest -ForUpgrade
        $expected = if ($sameBuild) { $oldHash } else { $newHash }
        Assert-Test ((Get-Sha256 $target) -eq $expected) "Incorrect restore in $scenario"
        Assert-Test (-not (Test-Path -LiteralPath $stateFile)) 'Completed restore retained stale state'
        Assert-Test (-not (Test-Path -LiteralPath $cache)) 'Retired compiled cache survived upgrade'
    }
    Assert-Test ((Get-Sha256 $saved) -eq $oldHash) 'Original backup was modified'
    Write-Host "PASS $scenario" -ForegroundColor Green
}
Write-Host "Upgrade restore regression fixtures: $testRoot"
