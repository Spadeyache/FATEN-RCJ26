# ============================================================================
# convert_model.ps1  --  ONE command: ONNX -> kmodel + report + deploy.
#
# Default = the verified BEST config (NoClip uint8/uint8, nncase 2.11).
#   .\convert_model.ps1 -Name victim_v2                       # best config (fast)
#   .\convert_model.ps1 -Name victim_v2 -Search              # sweep 5 configs, pick best
#   .\convert_model.ps1 -Name victim_v2 -EvalLimit 30        # verify on more images
#
# PTQ CALIBRATION (sets the quantization ranges -> match what the board sees):
#   -CalibDir <folder>     images to calibrate on (no labels needed).
#                          Default: datasets/<Name>/val/images
#                          ON COMP DAY: point this at a folder of photos taken
#                          with the K230D camera at the venue (same lighting/field).
#   -CalibSamples <n>      how many calibration images to use (default 8).
#
#   # competition-day calibration example (photos in raw_export/day_photos/):
#   .\convert_model.ps1 -Name victim_v2 -CalibDir raw_export/day_photos -CalibSamples 20
# ============================================================================
param(
    [string]$Name = "victim",
    [int]$EvalLimit = 8,
    [string]$CalibDir = "",
    [int]$CalibSamples = 8,
    [switch]$Search
)
$ErrorActionPreference = "Stop"
$ff = $PSScriptRoot
$convImg = "k230-nncase:latest"
$opencv = "pip install -q opencv-python-headless==4.10.0.84 >/dev/null 2>&1;"

docker info *> $null
if (-not $?) { Write-Host "Docker daemon not running. Start Docker Desktop." -ForegroundColor Red; exit 1 }
if (-not (Test-Path "$ff\models\best_640x480.onnx")) {
    Write-Host "models/best_640x480.onnx not found. Run .\train_model.ps1 -Name $Name first." -ForegroundColor Red; exit 1
}

# Calibration source priority:
#   1. explicit -CalibDir
#   2. datasets/<Name>/calibration/   (drop competition-day photos here; survives add_data)
#   3. datasets/<Name>/val/images     (fallback)
if ($CalibDir) {
    $calib = $CalibDir
} elseif (Test-Path "$ff\datasets\$Name\calibration") {
    $calib = "datasets/$Name/calibration"
    Write-Host "using competition-day calibration: $calib" -ForegroundColor Yellow
} else {
    $calib = "datasets/$Name/val/images"
}
if (-not (Test-Path "$ff\$calib")) {
    Write-Host "calib dir not found: $calib" -ForegroundColor Red; exit 1
}
$nImgs = (Get-ChildItem "$ff\$calib" -Include *.jpg,*.jpeg,*.png,*.bmp -Recurse -File -EA SilentlyContinue).Count
if ($nImgs -lt $CalibSamples) {
    Write-Host "calib dir has $nImgs images but -CalibSamples is $CalibSamples. Lower -CalibSamples or add images." -ForegroundColor Red; exit 1
}

if ($Search) { $mode = "convert-search --options 0,3,1,4,5"; $which = "searched" }
else         { $mode = "convert-preset";                     $which = "preset" }

Write-Host "=== $which kmodel + report + deploy  (calib $calib x$CalibSamples, eval $EvalLimit) [nncase 2.11] ===" -ForegroundColor Cyan
$cmd = @"
$opencv
python3 k230_pipeline.py --no-bootstrap $mode models/best_640x480.onnx --calib-data $calib --eval-data datasets/$Name/val/images --num-samples $CalibSamples --eval-limit $EvalLimit --img-height 480 --img-width 640 &&
python3 k230_pipeline.py --no-bootstrap report --data datasets/$Name/data.yaml --output report.html &&
python3 k230_pipeline.py --no-bootstrap deploy --which $which --data datasets/$Name/data.yaml --img-width 640 --img-height 480
"@
docker run --rm -v "${ff}:/workspace" -w /workspace $convImg bash -lc $cmd

Write-Host "`nDone.  kmodel: deploy/best.kmodel   report: report.html   bundle: deploy/" -ForegroundColor Green
