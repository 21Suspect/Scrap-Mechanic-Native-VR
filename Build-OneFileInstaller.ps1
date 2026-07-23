[CmdletBinding()]
param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot 'ScrapMechanicVR-Patcher.exe'
}
$csc = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc -PathType Leaf)) {
    throw 'The .NET Framework 4.x C# compiler was not found.'
}

$manifest = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') | ConvertFrom-Json
if ($manifest.game.executableSha256 -ne '9D8B5F97171413DB1C284FC36B983173CB08E272AF3DF427F7DCDA360A3FD1FE') {
    throw 'Refusing to build: the guarded game executable hash changed unexpectedly.'
}

& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Patcher.ps1') -Action ValidatePayload
if ($LASTEXITCODE -ne 0) {
    throw 'Payload validation failed.'
}

$workingRoot = Join-Path $env:TEMP ( 'ScrapMechanicVR-OneFile-' + [Guid]::NewGuid().ToString('N') )
$stage = Join-Path $workingRoot 'stage'
$zip = Join-Path $workingRoot 'installer-payload.zip'
try {
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    foreach ($file in @('Patcher.ps1', 'manifest.json', 'LICENSE', 'LEGAL.md', 'THIRD_PARTY_NOTICES.md')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $stage $file)
    }
    $licenseStage = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseStage | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'source\NativeVR\third_party\OpenXR-LICENSE') -Destination (Join-Path $licenseStage 'OpenXR-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'source\NativeVR\third_party\RESHade-LICENSE.md') -Destination (Join-Path $licenseStage 'ReShade-LICENSE.md')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'payload') -Destination (Join-Path $stage 'payload') -Recurse
    Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $stage -Force).FullName -DestinationPath $zip -CompressionLevel Optimal

    $outputDirectory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    $outputFullPath = [IO.Path]::GetFullPath($OutputPath)
    $source = Join-Path $PSScriptRoot 'installer\Program.cs'
    $applicationManifest = Join-Path $PSScriptRoot 'installer\app.manifest'

    $arguments = @(
        '/nologo',
        '/target:winexe',
        '/platform:anycpu',
        '/optimize+',
        "/win32manifest:$applicationManifest",
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
