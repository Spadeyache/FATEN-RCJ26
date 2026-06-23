# ============================================================================
# train_model.ps1  --  ONE command: train YOLOv8n on datasets/<Name>.
#
# Trains on the already-organized folder datasets/<Name> (run add_data.ps1
# first), then exports to ONNX. GPU container.
#
#     .\train_model.ps1 -Name victim_v2 -Epochs 100
#     .\train_model.ps1 -Name victim_v2 -SkipTrain     # reuse best.pt, export only
# ============================================================================
param(
    [string]$Name = "victim",
    [int]$Epochs = 100,
    [int]$Batch = 8,
    [switch]$SkipTrain,
    [switch]$NoExport
)
$ErrorActionPreference = "Stop"
$ff = $PSScriptRoot
$trainImg = "k230d-train:latest"

docker info *> $null
if (-not $?) { Write-Host "Docker daemon not running. Start Docker Desktop." -ForegroundColor Red; exit 1 }
if (-not (docker images -q $trainImg)) {
    Write-Host "Missing image $trainImg. Build: docker build -t $trainImg docker/train" -ForegroundColor Red; exit 1
}
if (-not (Test-Path "$ff\datasets\$Name\data.yaml")) {
    Write-Host "datasets/$Name not found. Run .\add_data.ps1 -Name $Name first." -ForegroundColor Red; exit 1
}

if (-not $SkipTrain) {
    Write-Host "=== train YOLOv8n on datasets/$Name ($Epochs epochs, batch $Batch) [GPU] ===" -ForegroundColor Cyan
    docker run --rm --gpus all --shm-size 8g -v "${ff}:/workspace" -w /workspace $trainImg `
        python k230_pipeline.py --no-bootstrap train `
        --data "datasets/$Name/data.yaml" --epochs $Epochs --batch $Batch `
        --img-height 480 --img-width 640 --name $Name --workers 4
} else {
    Write-Host "=== train SKIPPED -- reusing runs/$Name/weights/best.pt ===" -ForegroundColor Cyan
}

if (-not $NoExport) {
    Write-Host "=== export best.pt -> models/best_640x480.onnx [GPU] ===" -ForegroundColor Cyan
    docker run --rm --gpus all -v "${ff}:/workspace" -w /workspace $trainImg `
        python k230_pipeline.py --no-bootstrap export `
        "runs/$Name/weights/best.pt" --output models --img-height 480 --img-width 640 --opset 11
}

Write-Host "`nDone.  model: runs/$Name/weights/best.pt   onnx: models/best_640x480.onnx" -ForegroundColor Green
Write-Host "Next: .\convert_model.ps1 -Name $Name" -ForegroundColor Green
