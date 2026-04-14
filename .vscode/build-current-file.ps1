param(
    [Parameter(Mandatory=$true)]
    [string]$SourceFile
)

$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\tools\msys64\mingw64\bin;' + $env:PATH
$build = 'F:/Users/20968/projects/ai/gnn/examples/FluentCFFZoneViewer/build-msys2-clang'
$fileName = [System.IO.Path]::GetFileNameWithoutExtension($SourceFile)
$mainObj = "$build/$fileName.manual.obj"
$manualExe = "$build/$fileName.manual.exe"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Build current file: $SourceFile" -ForegroundColor Cyan
Write-Host "Output object: $mainObj" -ForegroundColor Cyan
Write-Host "Output exe: $manualExe" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$common = @(
    '-std=gnu++17','-fopenmp','-DKISSFFT_DLL_IMPORT=1','-Dkiss_fft_scalar=double',
    '-IF:/Users/20968/projects/ai/gnn/examples/FluentCFFZoneViewer/../../vtk/IO/FLUENTCFF',
    '-isystem','C:/tools/msys64/mingw64/include/vtk',
    '-isystem','C:/tools/msys64/mingw64/include/vtk/token',
    '-isystem','C:/tools/msys64/mingw64/include/vtk/vtkkissfft',
    '-isystem','C:/tools/msys64/mingw64/include/qt6',
    '-isystem','C:/tools/msys64/mingw64/include/qt6/QtCore',
    '-isystem','C:/tools/msys64/mingw64/include/qt6/QtGui',
    '-isystem','C:/tools/msys64/mingw64/include/qt6/QtWidgets',
    '-isystem','C:/tools/msys64/mingw64/include/qt6/QtOpenGL',
    '-isystem','C:/tools/msys64/mingw64/include/qt6/QtOpenGLWidgets',
    '-isystem','C:/tools/msys64/mingw64/include/qt6/QtConcurrent',
    '-idirafter','C:/ProgramData/anaconda3/Library/include'
)

# Write-Host "[1/2] Compiling source file..." -ForegroundColor Yellow
# try {
#     & 'C:\tools\msys64\mingw64\bin\clang++.exe' '-g' '-O0' @common -c $SourceFile -o $mainObj
#     Write-Host "[1/2] Compilation succeeded!" -ForegroundColor Green
# } catch {
#     Write-Host "[1/2] Compilation failed!" -ForegroundColor Red
#     throw $_
# }

# Write-Host "[3/2] Linking object files and libraries..." -ForegroundColor Yellow
$args = @(
    '-g', 
    $mainObj,
    '-o', $manualExe,
    '-Wl,--major-image-version,0,--minor-image-version,0',
    "$build/libLocalFLUENTCFFReader.manual.a",
    'C:/tools/msys64/mingw64/lib/libvtkGUISupportQt.dll.a'
    'C:/ProgramData/anaconda3/Library/lib/hdf5.lib',
    'C:/ProgramData/anaconda3/Library/lib/hdf5_hl.lib',
    'C:/tools/msys64/mingw64/lib/libQt6Concurrent.dll.a',
    'C:/tools/msys64/mingw64/lib/libQt6OpenGLWidgets.dll.a',
    'C:/tools/msys64/mingw64/lib/libQt6Widgets.dll.a',
    'C:/tools/msys64/mingw64/lib/libQt6OpenGL.dll.a',
    'C:/tools/msys64/mingw64/lib/libQt6Gui.dll.a',
    'C:/tools/msys64/mingw64/lib/libQt6Core.dll.a',
    '-lmpr','-luserenv','-ld3d11','-ldxgi','-ldxguid','-ld3d12',
    'C:/tools/msys64/mingw64/lib/libvtkInteractionWidgets.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkInteractionStyle.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkRenderingOpenGL2.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkRenderingHyperTreeGrid.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkRenderingUI.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkglad.dll.a',
    '-lopengl32',
    'C:/tools/msys64/mingw64/lib/libvtkRenderingContext2D.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkIOImage.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkRenderingCore.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkCommonColor.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkFiltersSources.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkImagingCore.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkFiltersGeneral.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkFiltersCore.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkCommonExecutionModel.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkCommonDataModel.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkCommonTransforms.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkCommonMisc.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkCommonMath.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtkCommonCore.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtktoken.dll.a',
    'C:/tools/msys64/mingw64/lib/libtbb12.dll.a',
    'C:/tools/msys64/mingw64/bin/libomp.dll',
    'C:/tools/msys64/mingw64/lib/libvtkkissfft.dll.a',
    'C:/tools/msys64/mingw64/lib/libvtksys.dll.a',
    '-lgdi32','-lws2_32','-lpsapi','-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32'
)

try {
    & 'C:\tools\msys64\mingw64\bin\clang++.exe' @args
    Write-Host "[2/2] Linking succeeded!" -ForegroundColor Green
} catch {
    Write-Host "[2/2] Linking failed!" -ForegroundColor Red
    throw $_
}

# Write-Host ""
# Write-Host "Output executable:" -ForegroundColor Green
# Get-Item $manualExe | Format-List FullName,Length,LastWriteTime
# Write-Host "========================================" -ForegroundColor Cyan
# Write-Host "Build completed successfully!" -ForegroundColor Green
# Write-Host "Ready for debugging." -ForegroundColor Green

