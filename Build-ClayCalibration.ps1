[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$csc = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc -PathType Leaf)) {
    throw "64-bit C# compiler not found: $csc"
}

$source = Join-Path $PSScriptRoot 'tools\ClayCalibration\Program.cs'
$output = Join-Path $PSScriptRoot 'payload\Release\ScrapMechanicVR-ClayCalibration.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ `
    /reference:System.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll `
    /out:$output $source
if ($LASTEXITCODE -ne 0) { throw "Clay calibration helper failed to compile: $LASTEXITCODE" }

$hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
Write-Host "Built $output"
Write-Host "SHA-256 $($hash.Hash)"
