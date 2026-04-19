cmake_minimum_required(VERSION 3.20)
project(FluentCFFGNNPy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer the repo-local Python venv (.\.venv) for Torch/pybind11 CMake packages + Torch DLLs.
# Override with -DFLUENTCFF_VENV_ROOT=... if your venv is not at <repo>/.venv
get_filename_component(_FLUENTCFF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(FLUENTCFF_VENV_ROOT "${_FLUENTCFF_REPO_ROOT}/.venv" CACHE PATH "Path to the project Python virtualenv (.venv)")
set(_FLUENTCFF_VENV_SITE "${FLUENTCFF_VENV_ROOT}/Lib/site-packages")
set(_FLUENTCFF_TORCH_LIB "${_FLUENTCFF_VENV_SITE}/torch/lib")
set(_FLUENTCFF_TORCH_CMAKE "${_FLUENTCFF_VENV_SITE}/torch/share/cmake/Torch")
set(_FLUENTCFF_PYBIND11_CMAKE "${_FLUENTCFF_VENV_SITE}/pybind11/share/cmake/pybind11")

if(NOT EXISTS "${FLUENTCFF_VENV_ROOT}")
  message(FATAL_ERROR "Python venv not found. Expected: ${FLUENTCFF_VENV_ROOT} (set -DFLUENTCFF_VENV_ROOT=...)")
endif()
if(NOT EXISTS "${_FLUENTCFF_TORCH_LIB}")
  message(FATAL_ERROR "Torch lib directory not found under venv: ${_FLUENTCFF_TORCH_LIB}")
endif()
if(NOT EXISTS "${_FLUENTCFF_TORCH_CMAKE}/TorchConfig.cmake")
  message(FATAL_ERROR "Torch CMake package not found under venv: ${_FLUENTCFF_TORCH_CMAKE}/TorchConfig.cmake")
endif()
if(NOT EXISTS "${_FLUENTCFF_PYBIND11_CMAKE}/pybind11Config.cmake")
  message(FATAL_ERROR "pybind11 CMake package not found under venv: ${_FLUENTCFF_PYBIND11_CMAKE}/pybind11Config.cmake")
endif()

list(PREPEND CMAKE_PREFIX_PATH
  "${_FLUENTCFF_VENV_SITE}"
  "${_FLUENTCFF_TORCH_CMAKE}"
  "${_FLUENTCFF_PYBIND11_CMAKE}"
)

# Interpreter used for pybind11 / extension build. Must match the venv where torch/pybind11
# were pip-installed; otherwise CMake may pick MSYS2's python from CMAKE_PREFIX_PATH (wrong ABI).
if(WIN32)
  set(_FLUENTCFF_PY_EXE "${FLUENTCFF_VENV_ROOT}/Scripts/python.exe")
else()
  set(_FLUENTCFF_PY_EXE "${FLUENTCFF_VENV_ROOT}/bin/python3")
endif()
if(NOT EXISTS "${_FLUENTCFF_PY_EXE}")
  message(FATAL_ERROR "Venv Python executable not found: ${_FLUENTCFF_PY_EXE}")
endif()
set(Python_EXECUTABLE "${_FLUENTCFF_PY_EXE}" CACHE FILEPATH "Python for pybind11 (venv)" FORCE)
set(PYBIND11_FINDPYTHON ON)

set(MSYS2_PREFIX "C:/tools/msys64/mingw64")

# Avoid accidentally picking up Conda toolchains.
list(APPEND CMAKE_IGNORE_PREFIX_PATH
  "C:/ProgramData/anaconda3"
  "C:/ProgramData/anaconda3/Library"
)

# MSVC builds must not resolve VTK/HDF5 from MSYS2 (PATH or CMAKE_PREFIX_PATH often injects these).
if(MSVC)
  list(APPEND CMAKE_IGNORE_PREFIX_PATH
    "C:/tools/msys64/mingw64"
    "C:/tools/msys64/mingw64/lib"
    "C:/tools/msys64/mingw64/lib/cmake"
    "C:/tools/msys64/mingw64/lib/cmake/vtk"
  )
  if(DEFINED VTK_DIR AND "${VTK_DIR}" MATCHES "[Mm]sys64")
    message(STATUS "FluentCFFGNNPy: clearing VTK_DIR (MSYS2 not usable with MSVC): ${VTK_DIR}")
    unset(VTK_DIR CACHE)
  endif()
endif()

# VTK + HDF5 must match the C++ compiler ABI:
# - pip PyTorch on Windows is MSVC → this extension must be built with MSVC → VTK/HDF5 must be MSVC (e.g. vcpkg x64-windows).
# - MSYS2 MinGW VTK headers MUST NOT be mixed with cl.exe (will pull in MinGW stdio.h / __asm__ and explode).
set(FLUENTCFF_MSVC_VCPKG_ROOT "" CACHE PATH
  "When using MSVC: vcpkg prefix, e.g. F:/path/to/vcpkg/installed/x64-windows or E:/vcpkg-work/.../installed/x64-windows (MSVC VTK + HDF5). If F: is low on space, build with cmake/FluentCFFGNNPy/install-vcpkg-deps-E.ps1 and point here to E: install root.")

# VS Code tasks / CI can export FLUENTCFF_MSVC_VCPKG_ROOT instead of passing -D each time.
if(WIN32 AND MSVC AND FLUENTCFF_MSVC_VCPKG_ROOT STREQUAL "" AND DEFINED ENV{FLUENTCFF_MSVC_VCPKG_ROOT})
  string(STRIP "$ENV{FLUENTCFF_MSVC_VCPKG_ROOT}" _fluentcff_msvc_vcpkg)
  if(NOT _fluentcff_msvc_vcpkg STREQUAL "")
    set(FLUENTCFF_MSVC_VCPKG_ROOT "${_fluentcff_msvc_vcpkg}" CACHE PATH "vcpkg MSVC prefix (from FLUENTCFF_MSVC_VCPKG_ROOT env)" FORCE)
    message(STATUS "FluentCFFGNNPy: FLUENTCFF_MSVC_VCPKG_ROOT from environment: ${FLUENTCFF_MSVC_VCPKG_ROOT}")
  endif()
endif()

if(MSVC)
  if(FLUENTCFF_MSVC_VCPKG_ROOT STREQUAL "")
    message(FATAL_ERROR
      "MSVC is the C++ compiler, but FLUENTCFF_MSVC_VCPKG_ROOT is not set.\n"
      "  MSYS2 MinGW VTK cannot be used here (headers/runtime are incompatible with MSVC + pip torch).\n"
      "  Install VTK + HDF5 for MSVC, e.g. with vcpkg triplet x64-windows, then configure:\n"
      "    cmake ... -DFLUENTCFF_MSVC_VCPKG_ROOT=C:/path/to/vcpkg/installed/x64-windows\n"
      "  Example (downloads/buildtrees/packages/installed on E:, avoids F: full disk):\n"
      "    powershell -ExecutionPolicy Bypass -File cmake/FluentCFFGNNPy/install-vcpkg-deps-E.ps1\n"
      "  Then delete the build dir and re-run cmake from a \"x64 Native Tools\" / VS Developer shell.")
  endif()
  if(NOT EXISTS "${FLUENTCFF_MSVC_VCPKG_ROOT}")
    message(FATAL_ERROR "FLUENTCFF_MSVC_VCPKG_ROOT does not exist: ${FLUENTCFF_MSVC_VCPKG_ROOT}")
  endif()
  list(PREPEND CMAKE_PREFIX_PATH "${FLUENTCFF_MSVC_VCPKG_ROOT}")
elseif(WIN32 AND (MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
  set(MSYS2_BIN "${MSYS2_PREFIX}/bin")
  if(NOT EXISTS "${MSYS2_BIN}")
    message(FATAL_ERROR "MSYS2 bin directory not found: ${MSYS2_BIN}")
  endif()
  list(PREPEND CMAKE_PREFIX_PATH
    "${MSYS2_PREFIX}"
    "${MSYS2_PREFIX}/lib/cmake"
    "${MSYS2_PREFIX}/lib/cmake/vtk"
  )
  message(FATAL_ERROR
    "MinGW/g++ was detected, but this project links pip's MSVC-built PyTorch C++ libraries.\n"
    "  Please configure from a Visual Studio MSVC environment (Ninja + cl, or VS generator) and set FLUENTCFF_MSVC_VCPKG_ROOT.\n"
    "  (A pure MinGW build would require a MinGW-compatible LibTorch and MSVC-free Python — not supported by this CMakeLists.)")
elseif(NOT WIN32)
  # Non-Windows: supply VTK via CMAKE_PREFIX_PATH / VTK_DIR in your environment.
  message(STATUS "FluentCFFGNNPy: non-Windows: expecting VTK on CMAKE_PREFIX_PATH")
else()
  message(FATAL_ERROR "Unsupported Windows toolchain: ${CMAKE_CXX_COMPILER_ID}")
endif()

find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  CommonExecutionModel
  FiltersCore
)

# Dependencies (preferred): pip-installed torch + pybind11 inside FLUENTCFF_VENV_ROOT.
set(Torch_DIR "${_FLUENTCFF_TORCH_CMAKE}" CACHE PATH "Torch CMake package directory" FORCE)
set(pybind11_DIR "${_FLUENTCFF_PYBIND11_CMAKE}" CACHE PATH "pybind11 CMake package directory" FORCE)
# New FindPython path (avoids FindPythonLibsNew picking MSYS2 python when MSVC is the CXX compiler).
find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

# Only pin a host CUDA Toolkit when the pip wheel actually ships CUDA pieces.
file(GLOB _FLUENTCFF_WHEEL_CUDA_MARKERS
  "${_FLUENTCFF_TORCH_LIB}/torch_cuda.dll"
  "${_FLUENTCFF_TORCH_LIB}/libtorch_cuda.so*"
  "${_FLUENTCFF_TORCH_LIB}/libtorch_cuda.dylib"
)
if(_FLUENTCFF_WHEEL_CUDA_MARKERS)
  set(_FLUENTCFF_PIP_TORCH_CUDA ON)
else()
  set(_FLUENTCFF_PIP_TORCH_CUDA OFF)
endif()
message(STATUS "FluentCFFGNNPy: pip torch CUDA artifacts present: ${_FLUENTCFF_PIP_TORCH_CUDA}")

if(_FLUENTCFF_PIP_TORCH_CUDA)
  # Pip-installed CUDA torch: Caffe2's public/cuda.cmake first runs legacy find_package(CUDA).
  # That path honors CUDA_TOOLKIT_ROOT_DIR / CUDA_HOME / CUDA_PATH and CUDA_NVCC_EXECUTABLE.
  if(WIN32)
    set(_FLUENTCFF_NVCC_DEFAULT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8/bin/nvcc.exe")
    set(_FLUENTCFF_CUDA_ROOT_DEFAULT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8")
  else()
    set(_FLUENTCFF_NVCC_DEFAULT "/usr/local/cuda/bin/nvcc")
    set(_FLUENTCFF_CUDA_ROOT_DEFAULT "/usr/local/cuda")
  endif()
  set(FLUENTCFF_NVCC "${_FLUENTCFF_NVCC_DEFAULT}"
    CACHE FILEPATH "Path to nvcc (optional; used to infer CUDA root)")
  set(FLUENTCFF_CUDA_ROOT "${_FLUENTCFF_CUDA_ROOT_DEFAULT}"
    CACHE PATH "CUDA toolkit root (directory that contains bin/, lib/, include/)")

  if(EXISTS "${FLUENTCFF_NVCC}")
    get_filename_component(_fluentcff_cuda_bin "${FLUENTCFF_NVCC}" DIRECTORY)
    get_filename_component(_fluentcff_cuda_root "${_fluentcff_cuda_bin}" DIRECTORY)
    if(EXISTS "${_fluentcff_cuda_root}/include/cuda.h")
      set(FLUENTCFF_CUDA_ROOT "${_fluentcff_cuda_root}" CACHE PATH "CUDA toolkit root (from FLUENTCFF_NVCC)" FORCE)
    endif()
  endif()

  if(NOT EXISTS "${FLUENTCFF_CUDA_ROOT}/include/cuda.h")
    find_program(_FLUENTCFF_NVCC_ONPATH NAMES nvcc)
    if(_FLUENTCFF_NVCC_ONPATH)
      get_filename_component(_fc_bin "${_FLUENTCFF_NVCC_ONPATH}" DIRECTORY)
      get_filename_component(_fc_root "${_fc_bin}" DIRECTORY)
      if(EXISTS "${_fc_root}/include/cuda.h")
        set(FLUENTCFF_NVCC "${_FLUENTCFF_NVCC_ONPATH}" CACHE FILEPATH "Path to nvcc (from PATH)" FORCE)
        set(FLUENTCFF_CUDA_ROOT "${_fc_root}" CACHE PATH "CUDA toolkit root (from nvcc on PATH)" FORCE)
        message(STATUS "FluentCFFGNNPy: auto-detected CUDA from nvcc on PATH: ${FLUENTCFF_CUDA_ROOT}")
      endif()
    endif()
  endif()

  if(WIN32 AND NOT EXISTS "${FLUENTCFF_CUDA_ROOT}/include/cuda.h")
    file(GLOB _FLUENTCFF_CUDA_HEADERS
      "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/*/include/cuda.h"
      "C:/Program Files (x86)/NVIDIA GPU Computing Toolkit/CUDA/*/include/cuda.h"
    )
    if(_FLUENTCFF_CUDA_HEADERS)
      list(SORT _FLUENTCFF_CUDA_HEADERS COMPARE NATURAL ORDER DESCENDING)
      list(GET _FLUENTCFF_CUDA_HEADERS 0 _fc_cuda_h)
      get_filename_component(_fc_inc "${_fc_cuda_h}" DIRECTORY)
      get_filename_component(_fc_root "${_fc_inc}" DIRECTORY)
      set(FLUENTCFF_CUDA_ROOT "${_fc_root}" CACHE PATH "CUDA toolkit root (auto-detected from Program Files)" FORCE)
      set(FLUENTCFF_NVCC "${_fc_root}/bin/nvcc.exe" CACHE FILEPATH "nvcc (auto-detected)" FORCE)
      message(STATUS "FluentCFFGNNPy: auto-detected CUDA toolkit: ${FLUENTCFF_CUDA_ROOT}")
    endif()
  elseif(UNIX AND NOT EXISTS "${FLUENTCFF_CUDA_ROOT}/include/cuda.h")
    file(GLOB _FLUENTCFF_CUDA_HEADERS_UNIX
      "/usr/local/cuda*/include/cuda.h"
      "/opt/cuda*/include/cuda.h"
    )
    if(_FLUENTCFF_CUDA_HEADERS_UNIX)
      list(SORT _FLUENTCFF_CUDA_HEADERS_UNIX COMPARE NATURAL ORDER DESCENDING)
      list(GET _FLUENTCFF_CUDA_HEADERS_UNIX 0 _fc_cuda_h)
      get_filename_component(_fc_inc "${_fc_cuda_h}" DIRECTORY)
      get_filename_component(_fc_root "${_fc_inc}" DIRECTORY)
      set(FLUENTCFF_CUDA_ROOT "${_fc_root}" CACHE PATH "CUDA toolkit root (auto-detected under /usr/local or /opt)" FORCE)
      set(FLUENTCFF_NVCC "${_fc_root}/bin/nvcc" CACHE FILEPATH "nvcc (auto-detected)" FORCE)
      message(STATUS "FluentCFFGNNPy: auto-detected CUDA toolkit: ${FLUENTCFF_CUDA_ROOT}")
    endif()
  endif()

  if(NOT EXISTS "${FLUENTCFF_CUDA_ROOT}/include/cuda.h")
    message(FATAL_ERROR
      "Could not locate a full CUDA Toolkit (missing \".../include/cuda.h\").\n"
      "  Tried FLUENTCFF_CUDA_ROOT=\"${FLUENTCFF_CUDA_ROOT}\", nvcc on PATH, and common install locations "
      "(Windows: Program Files NVIDIA CUDA tree; Linux: /usr/local/cuda* or /opt/cuda*).\n"
      "  Install the CUDA Toolkit, or set -DFLUENTCFF_CUDA_ROOT / -DFLUENTCFF_NVCC explicitly.\n"
      "  For CPU-only pip torch: pip install --force-reinstall torch --index-url https://download.pytorch.org/whl/cpu")
  endif()

  # Legacy FindCUDA + CUDAToolkit (PyTorch cuda.cmake uses both).
  set(CUDAToolkit_ROOT "${FLUENTCFF_CUDA_ROOT}" CACHE PATH "" FORCE)
  set(CUDA_TOOLKIT_ROOT_DIR "${FLUENTCFF_CUDA_ROOT}" CACHE PATH "" FORCE)
  set(CUDA_HOME "${FLUENTCFF_CUDA_ROOT}" CACHE PATH "" FORCE)
  if(WIN32)
    set(CUDA_NVCC_EXECUTABLE "${FLUENTCFF_CUDA_ROOT}/bin/nvcc.exe" CACHE FILEPATH "" FORCE)
  else()
    set(CUDA_NVCC_EXECUTABLE "${FLUENTCFF_CUDA_ROOT}/bin/nvcc" CACHE FILEPATH "" FORCE)
  endif()
  set(ENV{CUDA_PATH} "${FLUENTCFF_CUDA_ROOT}")
  set(ENV{CUDA_HOME} "${FLUENTCFF_CUDA_ROOT}")
  list(PREPEND CMAKE_PREFIX_PATH "${FLUENTCFF_CUDA_ROOT}")

  message(STATUS "FluentCFFGNNPy: FLUENTCFF_CUDA_ROOT=${FLUENTCFF_CUDA_ROOT}")
  message(STATUS "FluentCFFGNNPy: CUDA_TOOLKIT_ROOT_DIR=${CUDA_TOOLKIT_ROOT_DIR}")
  message(STATUS "FluentCFFGNNPy: CUDA_NVCC_EXECUTABLE=${CUDA_NVCC_EXECUTABLE}")
else()
  message(STATUS "FluentCFFGNNPy: CPU-only (or non-CUDA) pip torch; skipping CUDA toolkit pinning.")
endif()

# If Torch still cannot find CUDA, install a matching toolkit or use a CPU-only torch wheel:
#   pip install --force-reinstall torch --index-url https://download.pytorch.org/whl/cpu
find_package(Torch REQUIRED)

# Pip torch's TorchConfig.cmake defines imported target `torch` (lowercase) and TORCH_LIBRARIES;
# it does not define `Torch::Torch` (that name is used by some LibTorch / newer layouts).
if(TARGET torch)
  set(_FLUENTCFF_TORCH_LINK torch)
elseif(TARGET Torch::Torch)
  set(_FLUENTCFF_TORCH_LINK Torch::Torch)
else()
  message(FATAL_ERROR
    "find_package(Torch) succeeded but neither target 'torch' nor 'Torch::Torch' is defined. "
    "Check TORCH_LIBRARIES in CMake cache / torch wheel install.")
endif()
message(STATUS "FluentCFFGNNPy: linking PyTorch via target: ${_FLUENTCFF_TORCH_LINK}")

# torch/extension.h pulls in pybind11 specializations whose implementations live in
# torch_python (TORCH_PYTHON_API), not in torch.lib alone.
find_library(_FLUENTCFF_TORCH_PYTHON_LIB torch_python
  NAMES torch_python
  PATHS "${_FLUENTCFF_TORCH_LIB}"
  NO_DEFAULT_PATH
)
if(NOT _FLUENTCFF_TORCH_PYTHON_LIB)
  message(FATAL_ERROR
    "Could not find torch_python library next to pip torch (looked in \"${_FLUENTCFF_TORCH_LIB}\").")
endif()
message(STATUS "FluentCFFGNNPy: torch_python for pybind Tensor bindings: ${_FLUENTCFF_TORCH_PYTHON_LIB}")

if(MSVC)
  find_package(hdf5 CONFIG REQUIRED)
  if(TARGET hdf5::hdf5-static)
    set(_FLUENTCFF_HDF5_LINK hdf5::hdf5-static)
  elseif(TARGET hdf5::hdf5-shared)
    set(_FLUENTCFF_HDF5_LINK hdf5::hdf5-shared)
  elseif(TARGET hdf5::hdf5)
    set(_FLUENTCFF_HDF5_LINK hdf5::hdf5)
  else()
    message(FATAL_ERROR "hdf5 CONFIG package found but no hdf5::hdf5-static / hdf5::hdf5-shared target.")
  endif()
  message(STATUS "FluentCFFGNNPy: HDF5 (MSVC/vcpkg): ${_FLUENTCFF_HDF5_LINK}")
elseif(UNIX)
  find_package(HDF5 REQUIRED COMPONENTS C)
  set(_FLUENTCFF_HDF5_LINK HDF5::HDF5)
elseif(WIN32)
  set(HDF5_C_LIB "${MSYS2_PREFIX}/lib/libhdf5.dll.a")
  set(HDF5_HL_LIB "${MSYS2_PREFIX}/lib/libhdf5_hl.dll.a")
  if(NOT EXISTS "${HDF5_C_LIB}" OR NOT EXISTS "${HDF5_HL_LIB}")
    message(FATAL_ERROR "MSYS2 HDF5 import libraries not found under ${MSYS2_PREFIX}/lib")
  endif()
  set(_FLUENTCFF_HDF5_LINK "${HDF5_C_LIB}" "${HDF5_HL_LIB}")
else()
  message(FATAL_ERROR "HDF5: unsupported platform for this CMakeLists.")
endif()

# Build the reader as a static library (no dependency on FluentCFFZoneViewer).
add_library(LocalFLUENTCFFReader STATIC
  ../../vtk/IO/FLUENTCFF/vtkFLUENTCFFInternal.cxx
  ../../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx
  vtkAOSDataArrayTemplateInstantiate.cxx
)
target_compile_definitions(LocalFLUENTCFFReader PRIVATE
  VTK_USE_EXTERN_TEMPLATE=0
  VTKCOMMONCORE_STATIC_DEFINE
)
target_include_directories(LocalFLUENTCFFReader
  PUBLIC
    ../../vtk/IO/FLUENTCFF
)
target_link_libraries(LocalFLUENTCFFReader
  PUBLIC
    ${_FLUENTCFF_HDF5_LINK}
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::CommonExecutionModel
    VTK::FiltersCore
)

# Exporter (C++ logic) as a static library.
add_library(FluentCFFGNNExporter STATIC
  ../../FluentCFFGNN/FluentCFFGNNExporter.cxx
)
target_include_directories(FluentCFFGNNExporter
  PUBLIC
    ../../FluentCFFGNN
    ../../vtk/IO/FLUENTCFF
)
target_link_libraries(FluentCFFGNNExporter
  PUBLIC
    LocalFLUENTCFFReader
    ${_FLUENTCFF_TORCH_LINK}
)
target_link_directories(FluentCFFGNNExporter PUBLIC "${_FLUENTCFF_TORCH_LIB}")

# Python extension module (.pyd)
pybind11_add_module(fluentcff_gnn
  ../../python/fluentcff_gnn_pybind.cpp
)
target_compile_definitions(fluentcff_gnn PRIVATE
  VTK_USE_EXTERN_TEMPLATE=0
  VTKCOMMONCORE_STATIC_DEFINE
  # Required when including torch/extension.h so pybind11 Tensor cast symbols link (PyTorch extension ABI).
  TORCH_EXTENSION_NAME=fluentcff_gnn
)
target_link_libraries(fluentcff_gnn
  PRIVATE
    FluentCFFGNNExporter
    LocalFLUENTCFFReader
    ${_FLUENTCFF_TORCH_LINK}
    "${_FLUENTCFF_TORCH_PYTHON_LIB}"
)
target_link_directories(fluentcff_gnn PRIVATE "${_FLUENTCFF_TORCH_LIB}")

# Default: put fluentcff_gnn*.pyd in the build tree so python/smoke_test can add build/fluentcff_gnn_py to sys.path.
set(FLUENTCFF_PYD_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" CACHE PATH
  "Directory for fluentcff_gnn extension (.pyd). Default CMAKE_BINARY_DIR matches CMakePresets / build/fluentcff_gnn_py.")
if(CMAKE_CONFIGURATION_TYPES)
  foreach(_fluentcff_cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${_fluentcff_cfg} _fluentcff_CFGU)
    set_target_properties(fluentcff_gnn PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${_fluentcff_CFGU} "${FLUENTCFF_PYD_OUTPUT_DIRECTORY}"
      LIBRARY_OUTPUT_DIRECTORY_${_fluentcff_CFGU} "${FLUENTCFF_PYD_OUTPUT_DIRECTORY}")
  endforeach()
else()
  set_target_properties(fluentcff_gnn PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FLUENTCFF_PYD_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${FLUENTCFF_PYD_OUTPUT_DIRECTORY}")
endif()

# Help Windows find Torch/OpenMP DLLs at runtime when launching from MSVC/VS without manual PATH tweaks.
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT fluentcff_gnn)
set(CMAKE_MSVCIDE_RUN_PATH "${_FLUENTCFF_TORCH_LIB};$ENV{PATH}")

if(MSVC)
  target_compile_options(LocalFLUENTCFFReader PRIVATE /utf-8)
  target_compile_options(FluentCFFGNNExporter PRIVATE /utf-8)
  target_compile_options(fluentcff_gnn PRIVATE /utf-8 /bigobj)
endif()

