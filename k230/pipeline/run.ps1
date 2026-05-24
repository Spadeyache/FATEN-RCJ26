# PowerShell equivalent of the Makefile.
# Use on Windows (where `make` is typically not installed).
#
# Usage:
#   .\run.ps1 build
#   .\run.ps1 smoke
#   .\run.ps1 train
#   .\run.ps1 export
#   .\run.ps1 calibrate
#   .\run.ps1 compile
#   .\run.ps1 eval
#   .\run.ps1 select
#   .\run.ps1 package
#   .\run.ps1 all
#   .\run.ps1 all-skip-train
#   .\run.ps1 clean

param(
    [Parameter(Position=0)]
    [ValidateSet('help','build','smoke','train','export','calibrate','compile','eval','select','package','all','all-skip-train','clean')]
    [string]$Target = 'help'
)

# Don't bail on stderr noise from `docker` (it writes "Creating", etc. to stderr).
# We check $LASTEXITCODE explicitly after each native call.
$ErrorActionPreference = 'Continue'
$DockerDir = Join-Path $PSScriptRoot '..\docker'
$ComposeFile = Join-Path $DockerDir 'docker-compose.yml'

function Assert-Ok {
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED with exit $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

function Invoke-Train([string]$Script, [string[]]$ScriptArgs = @()) {
    Write-Host ">> train: $Script $($ScriptArgs -join ' ')" -ForegroundColor Cyan
    # Stream stderr to stdout, but DON'T pipe through ForEach — that clobbers $LASTEXITCODE.
    & docker compose -f $ComposeFile run --rm train python $Script @ScriptArgs 2>&1 | Out-Host
    Assert-Ok
}

function Invoke-Nncase([string]$Cmd) {
    # Ensure pip deps + PATH inside the nncase container before running the python step
    $wrap = "pip install -q opencv-python-headless pyyaml >/dev/null 2>&1; export PATH=/usr/local/lib/python3.10/dist-packages:`$PATH; cd /pipeline && $Cmd"
    Write-Host ">> nncase: $Cmd" -ForegroundColor Cyan
    & docker compose -f $ComposeFile run --rm nncase bash -c $wrap 2>&1 | Out-Host
    Assert-Ok
}

switch ($Target) {
    'help' {
        Write-Host "K230 pipeline runner (PowerShell)" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  build             Build both Docker images"
        Write-Host "  smoke             Stage 01 smoke-train (1 epoch)"
        Write-Host "  train             Stage 01 full train"
        Write-Host "  export            Stage 02 .pt -> .onnx"
        Write-Host "  calibrate         Stage 03 pick N calibration images"
        Write-Host "  compile           Stage 04 .onnx -> .kmodel (all variants)"
        Write-Host "  eval              Stage 05 PC eval each variant"
        Write-Host "  select            Stage 06 pick winner"
        Write-Host "  package           Stage 07 emit SD-card bundle"
        Write-Host "  all               Full pipeline (train -> ... -> package)"
        Write-Host "  all-skip-train    Pipeline without training (uses existing best.pt)"
        Write-Host "  clean             Wipe artifacts/"
        Write-Host ""
        Write-Host "Examples:"
        Write-Host "  .\run.ps1 all-skip-train"
        Write-Host "  .\run.ps1 calibrate; .\run.ps1 compile; .\run.ps1 eval"
    }
    'build'          { & docker compose -f $ComposeFile build 2>&1 | Out-Host; Assert-Ok }
    'smoke'          { Invoke-Train 'stages/01_train.py' @('--smoke') }
    'train'          { Invoke-Train 'stages/01_train.py' }
    'export'         { Invoke-Train 'stages/02_export.py' }
    'calibrate'      { Invoke-Nncase 'python stages/03_calibrate.py' }
    'compile'        { Invoke-Nncase 'python stages/04_compile.py' }
    'eval'           { Invoke-Nncase 'python stages/05_eval.py' }
    'select'         { Invoke-Nncase 'python stages/06_select.py' }
    'package'        { Invoke-Nncase 'python stages/07_package.py' }
    'all' {
        & $PSCommandPath train
        & $PSCommandPath export
        & $PSCommandPath calibrate
        & $PSCommandPath compile
        & $PSCommandPath eval
        & $PSCommandPath select
        & $PSCommandPath package
        Write-Host "`nDONE -> artifacts/deploy/" -ForegroundColor Green
    }
    'all-skip-train' {
        & $PSCommandPath export
        & $PSCommandPath calibrate
        & $PSCommandPath compile
        & $PSCommandPath eval
        & $PSCommandPath select
        & $PSCommandPath package
        Write-Host "`nDONE -> artifacts/deploy/  (used existing best.pt)" -ForegroundColor Green
    }
    'clean' {
        $art = Join-Path $PSScriptRoot 'artifacts'
        Remove-Item -Recurse -Force "$art\exports", "$art\reports", "$art\deploy", "$art\calib", "$art\best.onnx" -ErrorAction SilentlyContinue
        Write-Host "Cleaned artifacts/" -ForegroundColor Green
    }
}
