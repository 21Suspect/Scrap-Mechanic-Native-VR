[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build\release'),
    [string]$CCompiler = 'cc.exe',
    [string]$CxxCompiler = 'c++.exe',
    [string]$Ninja = 'ninja.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

foreach ($tool in @($CCompiler, $CxxCompiler, $Ninja, 'cmake.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required build tool was not found: $tool"
    }
}

cmake.exe -S $PSScriptRoot -B $BuildDirectory -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$CCompiler" `
    "-DCMAKE_CXX_COMPILER=$CxxCompiler" `
    "-DCMAKE_MAKE_PROGRAM=$Ninja"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }

cmake.exe --build $BuildDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }

$artifact = Join-Path $BuildDirectory 'smvr_native_vr_v1.addon64'
if (-not (Test-Path -LiteralPath $artifact)) { throw "Build artifact is missing: $artifact" }
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash
Write-Host "Built $artifact"
Write-Host "SHA256 $hash"
