# ============================================================================
# add_data.ps1  --  ONE command: combine raw_export/ -> datasets/<Name>.
#
# Drop new annotated export folder(s) into raw_export/ (each: images/ labels/
# classes.txt). This combines ALL of them (old + new), remaps labels by class
# name when needed, and splits into train/val. Existing calibration/ is
# preserved unless -CalibCount is set.
#
#     .\add_data.ps1 -Name victim_v2
# ============================================================================
param(
    [string]$Name = "victim",
    [double]$TrainRatio = 0.85,
    [int]$CalibCount = 0
)
$ErrorActionPreference = "Stop"
$ff = $PSScriptRoot

Write-Host "=== add data: raw_export/ -> datasets/$Name  (class-name remap checked) ===" -ForegroundColor Cyan
$pipelineArgs = @(
    "$ff\k230_pipeline.py",
    "organize",
    "--out", "datasets/$Name",
    "--train-ratio", $TrainRatio,
    "--calib-count", $CalibCount,
    "--force"
)

$ranPython = $false
if (Get-Command py -ErrorAction SilentlyContinue) {
    $ranPython = $true
    py -3 @pipelineArgs
}
if ((-not $ranPython -or $LASTEXITCODE -ne 0) -and (Get-Command python -ErrorAction SilentlyContinue)) {
    $ranPython = $true
    python @pipelineArgs
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "`nNext: .\train_model.ps1 -Name $Name" -ForegroundColor Green
