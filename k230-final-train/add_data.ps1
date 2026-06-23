# ============================================================================
# add_data.ps1  --  ONE command: combine raw_export/ -> datasets/<Name>.
#
# Drop new annotated export folder(s) into raw_export/ (each: images/ labels/
# classes.txt). This combines ALL of them (old + new, deduped by filename) and
# splits into train/val. It VERIFIES every export's classes.txt matches before
# combining (class IDs are positional — a mismatch would mislabel everything).
#
#     .\add_data.ps1 -Name victim_v2
# ============================================================================
param(
    [string]$Name = "victim",
    [double]$TrainRatio = 0.85
)
$ErrorActionPreference = "Stop"
$ff = $PSScriptRoot

Write-Host "=== add data: raw_export/ -> datasets/$Name  (class-match checked) ===" -ForegroundColor Cyan
py -3 "$ff\k230_pipeline.py" organize --out "datasets/$Name" --train-ratio $TrainRatio --force
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "`nNext: .\train_model.ps1 -Name $Name" -ForegroundColor Green
