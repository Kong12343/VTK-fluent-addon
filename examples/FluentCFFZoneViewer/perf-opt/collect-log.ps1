param(
  [string]$LogPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\..\.."))
$buildDir = Join-Path $projectRoot "examples\FluentCFFZoneViewer\build-msys2-clang"

if ($LogPath -eq "") {
  $LogPath = Join-Path $buildDir "FluentCFFZoneViewer-debug.log"
}
if (-not (Test-Path $LogPath)) {
  throw "Debug log not found: $LogPath"
}

$reportDir = Join-Path $buildDir "perf-opt-reports"
New-Item -Path $reportDir -ItemType Directory -Force | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$snapshotPath = Join-Path $reportDir "FluentCFFZoneViewer-debug-$stamp.log"
$deltaPath = Join-Path $reportDir "delta-$stamp.log"
$summaryPath = Join-Path $reportDir "summary-$stamp.txt"

Copy-Item -Path $LogPath -Destination $snapshotPath -Force

$allLines = Get-Content -Path $snapshotPath -Encoding UTF8
$markerPattern = "\[perf-opt\]\[marker\] run-v21 start"
$deltaStart = 0
for ($i = $allLines.Count - 1; $i -ge 0; $i--) {
  if ($allLines[$i] -match $markerPattern) {
    $deltaStart = $i
    break
  }
}

if ($allLines.Count -gt 0) {
  $deltaLines = $allLines[$deltaStart..($allLines.Count - 1)]
} else {
  $deltaLines = @()
}

$deltaLines | Set-Content -Path $deltaPath -Encoding UTF8

$patterns = @(
  "UpdateInformation",
  "Update [0-9]+ ms",
  "polyhedron vtkIdTypeArray",
  "cell data vtkDoubleArray WritePointer",
  "CreateFaceZonePolyData"
)

$deltaLines | Select-String -Pattern $patterns | ForEach-Object { $_.Line } | Set-Content -Path $summaryPath -Encoding utf8

Write-Host "Snapshot:" $snapshotPath
Write-Host "Delta:" $deltaPath
Write-Host "Summary:" $summaryPath
