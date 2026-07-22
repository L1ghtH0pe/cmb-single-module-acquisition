param(
    [int]$Port = 9000,
    [int]$Frames = 100,
    [double]$Timeout = 30.0
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
python (Join-Path $ProjectRoot 'tools\run_local_smoke.py') --port $Port --frames $Frames --timeout $Timeout
exit $LASTEXITCODE
