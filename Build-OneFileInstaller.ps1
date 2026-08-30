[CmdletBinding()]
param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot 'dist\ScrapMechanicVR-Installer.exe'
}
$csc = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc -PathType Leaf)) {
    throw 'The .NET Framework 4.x C# compiler was not found.'
}

$manifest = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') | ConvertFrom-Json
if ($manifest.game.executableSha256 -ne '5D663BA2EC5DC8C7ABEFCC5C9344AE86F0A066C4069A91F54833524AC9A5B4F5') {
    throw 'Refusing to build: the guarded game executable hash changed unexpectedly.'
}

$windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if (-not (Test-Path -LiteralPath $windowsPowerShell -PathType Leaf)) {
    throw 'Windows PowerShell 5.1 was not found.'
}
& $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Patcher.ps1') -Action ValidatePayload
if ($LASTEXITCODE -ne 0) {
    throw 'Payload validation failed.'
}

$iconPath = Join-Path $PSScriptRoot 'installer\ScrapMechanicVR.ico'
& $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'tools\Build-InstallerIcon.ps1') -OutputPath $iconPath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $iconPath -PathType Leaf)) {
    throw 'Installer icon generation failed.'
}

$musicSource = Join-Path $PSScriptRoot 'assets\audio\BOOMBOX MIX NEW.mp3'
if (-not (Test-Path -LiteralPath $musicSource -PathType Leaf)) {
    throw 'The packaged installer music is missing.'
}
$logoSource = Join-Path $PSScriptRoot 'assets\ScrapMechanicVR-Logo.png'
if (-not (Test-Path -LiteralPath $logoSource -PathType Leaf)) {
    throw 'The packaged installer logo is missing.'
}

# Keep the installer's embedded trust anchors synchronized with the exact files
# it is about to package. A stale constant produces an installer that compiles
# successfully but rejects its own payload during extraction.
$source = Join-Path $PSScriptRoot 'installer\Program.cs'
$programText = Get-Content -Raw -LiteralPath $source
function Assert-EmbeddedString([string]$Name, [string]$Expected) {
    $pattern = 'internal const string ' + [Regex]::Escape($Name) + ' = "([^"]+)";'
    $match = [Regex]::Match($programText, $pattern)
    if (-not $match.Success -or $match.Groups[1].Value -ne $Expected) {
        throw "installer/Program.cs $Name is stale. Expected '$Expected'."
    }
}
function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
$addonSource = Join-Path $PSScriptRoot 'payload\Release\smvr_native_vr_v1.addon64'
$dxgiSource = Join-Path $PSScriptRoot 'payload\Release\dxgi.dll'
$loaderSource = Join-Path $PSScriptRoot 'payload\Release\libopenxr_loader.dll'
Assert-EmbeddedString 'Version' ([string]$manifest.patchVersion)
Assert-EmbeddedString 'GameBuild' ([string]$manifest.game.buildId)
Assert-EmbeddedString 'GameExeHash' ([string]$manifest.game.executableSha256)
Assert-EmbeddedString 'AddonHash' (Get-Sha256 $addonSource)
Assert-EmbeddedString 'DxgiHash' (Get-Sha256 $dxgiSource)
Assert-EmbeddedString 'LoaderHash' (Get-Sha256 $loaderSource)
Assert-EmbeddedString 'MusicHash' (Get-Sha256 $musicSource)
Assert-EmbeddedString 'LogoHash' (Get-Sha256 $logoSource)
Assert-EmbeddedString 'ManifestHash' (Get-Sha256 (Join-Path $PSScriptRoot 'manifest.json'))
Assert-EmbeddedString 'PatcherHash' (Get-Sha256 (Join-Path $PSScriptRoot 'Patcher.ps1'))
$managedCountMatch = [Regex]::Match($programText, 'internal const int ManagedFileCount = ([0-9]+);')
if (-not $managedCountMatch.Success -or [int]$managedCountMatch.Groups[1].Value -ne @($manifest.files).Count) {
    throw "installer/Program.cs ManagedFileCount is stale. Expected $(@($manifest.files).Count)."
}

$workingRoot = Join-Path $env:TEMP ( 'ScrapMechanicVR-Chapter2-OneFile-' + [Guid]::NewGuid().ToString('N') )
$stage = Join-Path $workingRoot 'stage'
$zip = Join-Path $workingRoot 'installer-payload.zip'
try {
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    foreach ($file in @('Patcher.ps1', 'manifest.json', 'snapshot.json', 'LICENSE', 'LEGAL.md', 'THIRD_PARTY_NOTICES.md')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $stage $file)
    }
    $licenseStage = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseStage | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party\openxr\LICENSE') -Destination (Join-Path $licenseStage 'OpenXR-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party\reshade\LICENSE.md') -Destination (Join-Path $licenseStage 'ReShade-LICENSE.md')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party\minhook\LICENSE.txt') -Destination (Join-Path $licenseStage 'MinHook-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party\llvm\LICENSE.TXT') -Destination (Join-Path $licenseStage 'LLVM-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'payload') -Destination (Join-Path $stage 'payload') -Recurse
    $mediaStage = Join-Path $stage 'media'
    New-Item -ItemType Directory -Force -Path $mediaStage | Out-Null
    Copy-Item -LiteralPath $musicSource -Destination (Join-Path $mediaStage 'BOOMBOX MIX NEW.mp3')
    Copy-Item -LiteralPath $logoSource -Destination (Join-Path $mediaStage 'ScrapMechanicVR-Logo.png')
    Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $stage -Force).FullName -DestinationPath $zip -CompressionLevel Optimal

    $outputDirectory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    $outputFullPath = [IO.Path]::GetFullPath($OutputPath)
    $applicationManifest = Join-Path $PSScriptRoot 'installer\app.manifest'

    $arguments = @(
        '/nologo',
        '/target:winexe',
        '/platform:anycpu',
        '/optimize+',
        "/win32manifest:$applicationManifest",
        "/win32icon:$iconPath",
        "/resource:$zip,ScrapMechanicVR.Payload.zip",
        '/reference:System.dll',
        '/reference:System.Core.dll',
        '/reference:System.Drawing.dll',
        '/reference:System.Windows.Forms.dll',
        '/reference:System.IO.Compression.dll',
        '/reference:System.IO.Compression.FileSystem.dll',
        "/out:$outputFullPath",
        $source
    )
    & $csc $arguments
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $outputFullPath -PathType Leaf)) {
        throw 'The one-file installer failed to compile.'
    }

    $hash = (Get-FileHash -LiteralPath $outputFullPath -Algorithm SHA256).Hash
    Write-Host "Built: $outputFullPath" -ForegroundColor Green
    Write-Host "Size: $((Get-Item -LiteralPath $outputFullPath).Length) bytes" -ForegroundColor Green
    Write-Host "SHA-256: $hash" -ForegroundColor Green
}
finally {
    if (Test-Path -LiteralPath $workingRoot) {
        Remove-Item -LiteralPath $workingRoot -Recurse -Force
    }
}
