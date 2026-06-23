$ErrorActionPreference = 'Stop'

$streamDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$modePath = Join-Path $streamDir '..\modes\ModeLineFollow.cpp'
$outPath = Join-Path $streamDir 'stream_guides.generated.js'

$src = Get-Content -Raw -Path $modePath

function Get-Const($name) {
    $pattern = "constexpr\s+(?:uint8_t|uint16_t|int)\s+$name\s*=\s*([A-Za-z0-9_]+)"
    $m = [regex]::Match($src, $pattern)
    if (-not $m.Success) { throw "Missing constexpr $name in $modePath" }
    $value = $m.Groups[1].Value
    if ($value -match '^[0-9]+$') { return [int]$value }
    return Get-Const $value
}

$values = @{
    ARC_TOP_X = Get-Const 'ARC_TOP_X'
    ARC_TOP_Y = Get-Const 'ARC_TOP_Y'
    ARC_LEFT_X = Get-Const 'ARC_LEFT_X'
    ARC_RIGHT_X = Get-Const 'ARC_RIGHT_X'
    ARC_SIDE_Y = Get-Const 'ARC_SIDE_Y'
    ARC_BOTTOM_LEFT_X = Get-Const 'ARC_BOTTOM_LEFT_X'
    ARC_BOTTOM_Y = Get-Const 'ARC_BOTTOM_Y'
    ARC_BOTTOM_RIGHT_X = Get-Const 'ARC_BOTTOM_RIGHT_X'
    SILVER_COL_LEFT = Get-Const 'SILVER_COL_LEFT'
    SILVER_COL_RIGHT = Get-Const 'SILVER_COL_RIGHT'
    SILVER_ROW_MIN = Get-Const 'SILVER_ROW_MIN'
    SILVER_ROW_MAX = Get-Const 'SILVER_ROW_MAX'
    COLOR_X_MIN = Get-Const 'COLOR_X_MIN'
    COLOR_X_MAX = Get-Const 'COLOR_X_MAX'
    COLOR_ROW = Get-Const 'COLOR_ROW'
}

$js = @"
// Generated from ../modes/ModeLineFollow.cpp by update_stream_guides.ps1.
// Keep this file next to esp32_camera_viewer.html so the viewer can load it
// without asking the XIAO to spend serial bandwidth on static geometry.
window.XIAO_STREAM_GUIDES = {
  arc: [$($values.ARC_TOP_X), $($values.ARC_TOP_Y), $($values.ARC_LEFT_X), $($values.ARC_SIDE_Y), $($values.ARC_RIGHT_X), $($values.ARC_SIDE_Y), $($values.ARC_BOTTOM_LEFT_X), $($values.ARC_BOTTOM_Y), $($values.ARC_BOTTOM_RIGHT_X), $($values.ARC_BOTTOM_Y)],
  silverCols: [
    { x: $($values.SILVER_COL_LEFT), y0: $($values.SILVER_ROW_MIN), y1: $($values.SILVER_ROW_MAX) },
    { x: $($values.SILVER_COL_RIGHT), y0: $($values.SILVER_ROW_MIN), y1: $($values.SILVER_ROW_MAX) },
  ],
  colorLine: { x0: $($values.COLOR_X_MIN), x1: $($values.COLOR_X_MAX), y: $($values.COLOR_ROW) },
};
"@

Set-Content -Path $outPath -Value $js
Write-Host "Wrote $outPath"
