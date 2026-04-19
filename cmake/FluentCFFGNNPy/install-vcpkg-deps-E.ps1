# Installs vcpkg ports with downloads/buildtrees/packages/installed on E: (F: may be full).
# Run from VS Developer PowerShell (MSVC on PATH). Example:
#   powershell -NoProfile -ExecutionPolicy Bypass -File cmake\FluentCFFGNNPy\install-vcpkg-deps-E.ps1
# Then configure FluentCFFGNNPy with:
#   -DFLUENTCFF_MSVC_VCPKG_ROOT=E:/vcpkg-work/fluentcff-gnn/installed/x64-windows

$ErrorActionPreference = 'Stop'

$VcpkgRoot = 'F:\Users\20968\tools\vcpkg'
$WorkRoot = 'E:\vcpkg-work\fluentcff-gnn'
$VsRoot = 'F:\VS'

if (-not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
  throw "vcpkg.exe not found: $VcpkgRoot\vcpkg.exe"
}
if (-not (Test-Path "$VsRoot\VC\Auxiliary\Build\vcvarsall.bat")) {
  throw "Visual Studio root not found or incomplete: $VsRoot"
}

New-Item -ItemType Directory -Force -Path @(
  "$WorkRoot\downloads",
  "$WorkRoot\buildtrees",
  "$WorkRoot\packages",
  "$WorkRoot\installed"
) | Out-Null

# vcpkg invokes the first cmake.exe on PATH; MSYS MinGW cmake breaks x64-windows ports.
$vsCmakeBin = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
if (-not (Test-Path (Join-Path $vsCmakeBin 'cmake.exe'))) {
  throw @"
VS-bundled CMake not found: $vsCmakeBin\cmake.exe
Install the 'Desktop development with C++' workload and enable the CMake component in Visual Studio Installer.
"@
}
$pathNoMsys = @(
  foreach ($p in ($env:Path -split ';')) {
    if ($p -and $p -notmatch '(?i)msys64') { $p }
  }
)
$env:Path = ($vsCmakeBin + ';' + ($pathNoMsys -join ';')).TrimEnd(';')

$cmakeExe = (Get-Command cmake.exe -ErrorAction Stop).Source
if ($cmakeExe -match '(?i)msys64|mingw64\\bin') {
  throw @"
Resolved cmake.exe is still from MSYS/MinGW: $cmakeExe
Fix PATH (remove MSYS before cmake) or run this script from a clean VS Developer shell.
"@
}

$env:VCPKG_VISUAL_STUDIO_PATH = $VsRoot
Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue

Write-Host "VCPKG_VISUAL_STUDIO_PATH=$env:VCPKG_VISUAL_STUDIO_PATH"
Write-Host "WorkRoot=$WorkRoot"
Write-Host "cmake.exe = $cmakeExe"
where.exe cmake

& "$VcpkgRoot\vcpkg.exe" --vcpkg-root $VcpkgRoot install --triplet x64-windows vtk zlib --recurse `
  --downloads-root="$WorkRoot\downloads" `
  --x-buildtrees-root="$WorkRoot\buildtrees" `
  --x-packages-root="$WorkRoot\packages" `
  --x-install-root="$WorkRoot\installed"

Write-Host ""
Write-Host "Set CMake cache to:"
Write-Host "  -DFLUENTCFF_MSVC_VCPKG_ROOT=$WorkRoot\installed\x64-windows"
