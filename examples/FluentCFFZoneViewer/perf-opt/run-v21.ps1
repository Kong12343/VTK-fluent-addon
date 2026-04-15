param(
  [string]$CaseFile = "",
  [string]$DataFile = "",
  [switch]$KillExisting
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\..\.."))
$viewerDir = Join-Path $projectRoot "examples\FluentCFFZoneViewer"
$buildDir = Join-Path $viewerDir "build-msys2-clang"
$exePath = Join-Path $buildDir "FluentCFFZoneViewer.exe"

if ($CaseFile -eq "") {
  $CaseFile = Join-Path $projectRoot "data\v21\step-gamma-20-eta-1_5.cas.h5"
}
if ($DataFile -eq "") {
  $DataFile = Join-Path $projectRoot "data\v21\step-gamma-20-eta-1_5-x3-0_7.dat.h5"
}

if (-not (Test-Path $exePath)) {
  throw "Executable not found: $exePath"
}
if (-not (Test-Path $CaseFile)) {
  throw "Case file not found: $CaseFile"
}
if (-not (Test-Path $DataFile)) {
  throw "Data file not found: $DataFile"
}

if ($KillExisting) {
  Get-Process -Name "FluentCFFZoneViewer" -ErrorAction SilentlyContinue | Stop-Process -Force
}

# Runtime PATH hygiene: ensure MSYS2 + local build dir come first and avoid known conflicting runtimes.
$msysBin = "C:\tools\msys64\mingw64\bin"
$rawEntries = ($env:Path -split ";") | Where-Object { $_ -and $_.Trim().Length -gt 0 }
$cleanEntries = @()
$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

foreach ($entry in $rawEntries) {
  $trimmed = $entry.Trim()
  if ($trimmed -match "anaconda|miniconda|\\conda\\|\\ProgramData\\anaconda3") {
    continue
  }
  if (-not $seen.Contains($trimmed)) {
    [void]$seen.Add($trimmed)
    $cleanEntries += $trimmed
  }
}

$ordered = @($buildDir, $msysBin) + ($cleanEntries | Where-Object { $_ -ne $buildDir -and $_ -ne $msysBin })
$env:Path = ($ordered -join ";")

$preview = $ordered | Select-Object -First 5
Write-Host "Runtime PATH head:"
foreach ($p in $preview) {
  Write-Host "  $p"
}

$logPath = Join-Path $buildDir "FluentCFFZoneViewer-debug.log"
$reportDir = Join-Path $buildDir "perf-opt-reports"
New-Item -Path $reportDir -ItemType Directory -Force | Out-Null

$marker = "[{0}][perf-opt][marker] run-v21 start" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff")
Add-Content -Path $logPath -Value $marker -Encoding UTF8

$proc = Start-Process -FilePath $exePath -ArgumentList @($CaseFile, $DataFile) -WorkingDirectory $buildDir -PassThru
Write-Host "Viewer started. PID=$($proc.Id)"
Write-Host "Case: $CaseFile"
Write-Host "Data: $DataFile"
Write-Host "Marker: $marker"
