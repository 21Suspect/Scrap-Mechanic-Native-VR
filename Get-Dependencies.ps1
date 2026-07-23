[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$version = '0.16.0'
$archiveName = "zig-x86_64-windows-$version.zip"
$url = "https://ziglang.org/download/$version/$archiveName"
$expectedArchiveHash = '68659EB5F1E4EB1437A722F1DD889C5A322C9954607F5EDCF337BC3684A75A7E'
$toolsRoot = Join-Path $PSScriptRoot '.tools'
$archive = Join-Path $toolsRoot $archiveName
$installRoot = Join-Path $toolsRoot "zig-$version"
$zig = Join-Path $installRoot "zig-x86_64-windows-$version\zig.exe"

if (Test-Path -LiteralPath $zig) {
    $actualVersion = (& $zig version).Trim()
    if ($actualVersion -eq $version) {
        Write-Host "Zig $version is already installed: $zig" -ForegroundColor Green
        return
    }
    throw "Unexpected Zig version at ${zig}: $actualVersion"
}

New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Write-Host "Downloading pinned Zig $version from $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $archive
}

$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash
if ($actualHash -ne $expectedArchiveHash) {
    Remove-Item -LiteralPath $archive -Force
    throw "Zig archive hash mismatch. Expected $expectedArchiveHash, got $actualHash."
}

Remove-Item -LiteralPath $installRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $installRoot
if (-not (Test-Path -LiteralPath $zig)) {
    throw 'Zig extraction completed without producing zig.exe.'
}
if ((& $zig version).Trim() -ne $version) {
    throw 'Extracted Zig version does not match the pinned version.'
}
Write-Host "Installed checksum-verified Zig ${version}: $zig" -ForegroundColor Green
