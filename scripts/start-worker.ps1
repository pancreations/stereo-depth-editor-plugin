param([ValidateSet('auto','cpu','cuda')][string]$Device = 'auto', [switch]$Setup)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonPath = Join-Path $projectRoot '.venv310\Scripts\python.exe'
if ($env:S3D_PYTHON) { $pythonPath = $env:S3D_PYTHON }
Push-Location $projectRoot
try {
    if ($Setup) {
        $backend = if ($Device -eq 'cpu') { 'cpu' } else { 'cuda' }
        $venvPath = Split-Path -Parent (Split-Path -Parent $pythonPath)
        & py -3.10 tools/setup_worker.py --backend $backend --venv $venvPath
        if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
    }
    if (-not (Test-Path -LiteralPath $pythonPath)) { throw 'Run this script with -Setup first.' }
    & $pythonPath -m stereodepth_worker.server --device $Device --connection runtime/connection.json
    if ($LASTEXITCODE -ne 0) { throw 'Worker stopped with an error.' }
} finally { Pop-Location }
