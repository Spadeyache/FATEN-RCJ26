# ============================================================================
# competition_run.ps1  --  ONE command: the whole pipeline end to end.
#
# Composes the three step-scripts:
#   add_data.ps1     raw_export/ -> datasets/<Name>   (class-match checked)
#   train_model.ps1  train YOLOv8n + export ONNX      [GPU]
#   convert_model.ps1 ONNX -> kmodel (best) + report + deploy   [nncase 2.11]
#
# Drop new annotated export folder(s) into raw_export/, then:
#
#     .\competition_run.ps1 -Name victim_v2 -Epochs 100
#
# Flags:
#   -Name <str>      dataset + run name            (default: competition)
#   -Epochs <int>    training epochs               (default: 100)
#   -Batch <int>     batch size (4GB GPU -> 8)     (default: 8)
#   -EvalLimit <int> images used to verify kmodel  (default: 8)
#   -SkipTrain       reuse runs/<Name>/weights/best.pt (export+convert only)
#   -Search          sweep 5 PTQ configs instead of using the preset best
# ============================================================================
param(
    [string]$Name = "competition",
    [int]$Epochs = 100,
    [int]$Batch = 8,
    [int]$EvalLimit = 8,
    [string]$CalibDir = "",      # PTQ calibration images; default datasets/<Name>/val/images
    [int]$CalibSamples = 8,      # on comp day: point -CalibDir at venue photos
    [switch]$SkipTrain,
    [switch]$Search
)
$ErrorActionPreference = "Stop"
$ff = $PSScriptRoot

& "$ff\add_data.ps1"     -Name $Name
& "$ff\train_model.ps1"  -Name $Name -Epochs $Epochs -Batch $Batch -SkipTrain:$SkipTrain
& "$ff\convert_model.ps1" -Name $Name -EvalLimit $EvalLimit -CalibDir $CalibDir -CalibSamples $CalibSamples -Search:$Search

Write-Host "`n=== PIPELINE DONE ===" -ForegroundColor Green
Write-Host "  model  : runs/$Name/weights/best.pt"
Write-Host "  onnx   : models/best_640x480.onnx"
Write-Host "  kmodel : deploy/best.kmodel   (BEST exported model)"
Write-Host "  report : report.html"
Write-Host "  deploy : deploy/  (copy to K230D SD card /data/k230-final-train/)"
