param(
  [switch]$SkipKill,
  [int]$Jobs = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\..\.."))
$viewerDir = Join-Path $projectRoot "examples\FluentCFFZoneViewer"
$buildDir = Join-Path $viewerDir "build-msys2-clang"

# Runtime PATH hygiene: ensure MSYS2 goes first and filter known conflicting runtimes.
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

$ordered = @($msysBin) + ($cleanEntries | Where-Object { $_ -ne $msysBin })
$env:Path = ($ordered -join ";")

if (-not $SkipKill) {
  Get-Process -Name "FluentCFFZoneViewer" -ErrorAction SilentlyContinue | Stop-Process -Force
}

if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
  cmake -S $viewerDir -B $buildDir -DCMAKE_BUILD_TYPE=Debug
} else {
  cmake -S $viewerDir -B $buildDir
}

cmake --build $buildDir -j $Jobs

Write-Host "Build completed:" $buildDir
