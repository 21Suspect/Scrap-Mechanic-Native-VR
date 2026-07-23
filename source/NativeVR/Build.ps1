[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$distributionRoot = (Resolve-Path (Join-Path $projectRoot '..\..')).Path
$zig = $null
if ($env:SCRAPVR_ZIG -and (Test-Path -LiteralPath $env:SCRAPVR_ZIG)) {
    $zig = (Resolve-Path -LiteralPath $env:SCRAPVR_ZIG).Path
}
if (-not $zig) {
    $zigCommand = Get-Command zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($zigCommand) { $zig = $zigCommand.Source }
}
if (-not $zig) {
    $zig = Get-ChildItem (Join-Path $distributionRoot '.tools') -Recurse -Filter zig.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $zig) {
    throw 'Zig 0.16.0 was not found. Run Get-Dependencies.ps1 from the distribution root, put zig.exe on PATH, or set SCRAPVR_ZIG.'
}

$zigVersion = (& $zig version).Trim()
if ($zigVersion -ne '0.16.0') {
    throw "Unsupported Zig version $zigVersion. This project is pinned to 0.16.0."
}

$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force $buildDir | Out-Null
$output = Join-Path $buildDir 'scrap_native_vr.addon64'
$source = Join-Path $projectRoot 'src\addon.cpp'
$runtimeSource = Join-Path $projectRoot 'src\vr_runtime.cpp'
$handsSource = Join-Path $projectRoot 'src\vr_hands.cpp'
$toolsSource = Join-Path $projectRoot 'src\vr_tools.cpp'
$engineHooksSource = Join-Path $projectRoot 'src\engine_hooks.cpp'
$arguments = @(
    'c++',
    '-target', 'x86_64-windows-gnu',
    '-std=c++17',
    '-shared',
    '-O2',
    '-Wno-nullability-completeness',
    '-Wno-unknown-attributes',
    '-Wno-macro-redefined',
    '-DWIN32_LEAN_AND_MEAN',
    '-DNOMINMAX',
    '-DNDEBUG',
    '-I', (Join-Path $projectRoot 'third_party\reshade'),
    '-I', (Join-Path $projectRoot 'third_party'),
    '-o', $output,
    $source,
    $runtimeSource,
    $handsSource,
    $toolsSource,
    $engineHooksSource,
    '-lkernel32'
)

& $zig @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Native VR add-on build failed with exit code $LASTEXITCODE"
}

Get-FileHash -LiteralPath $output -Algorithm SHA256
