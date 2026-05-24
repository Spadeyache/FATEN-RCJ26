$ErrorActionPreference = "Stop"

$base = "https://raw.githubusercontent.com/kendryte/nncase/master/examples/user_guide"
$files = @(
    "k230_simulate-EN.ipynb",
    "nncase_base_func.py",
    "test.onnx",
    "test.tflite",
    "test.param"
)

foreach ($f in $files) {
    $dest = Join-Path $PSScriptRoot $f
    Write-Host "Downloading $f ..."
    Invoke-WebRequest -Uri "$base/$f" -OutFile $dest
}

Write-Host "Done. Files saved to $PSScriptRoot"
