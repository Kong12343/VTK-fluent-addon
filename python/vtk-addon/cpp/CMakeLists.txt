cmake_minimum_required(VERSION 3.20)
project(vtk_addon_fluentcff LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
  get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${SOURCE_ROOT}" SOURCE_ROOT)

set(FLUENTCFF_VENV_ROOT "${SOURCE_ROOT}/../../.venv" CACHE PATH "Path to the project Python virtualenv")
set(_FLUENTCFF_VENV_SITE "${FLUENTCFF_VENV_ROOT}/Lib/site-packages")
set(_FLUENTCFF_PYBIND11_CMAKE "${_FLUENTCFF_VENV_SITE}/pybind11/share/cmake/pybind11")

if(NOT DEFINED FLUENTCFF_MSVC_VCPKG_ROOT OR FLUENTCFF_MSVC_VCPKG_ROOT STREQUAL "")
  if(DEFINED ENV{FLUENTCFF_MSVC_VCPKG_ROOT} AND NOT "$ENV{FLUENTCFF_MSVC_VCPKG_ROOT}" STREQUAL "")
    set(FLUENTCFF_MSVC_VCPKG_ROOT "$ENV{FLUENTCFF_MSVC_VCPKG_ROOT}")
  else()
    set(FLUENTCFF_MSVC_VCPKG_ROOT "E:/vcpkg-work/fluentcff-gnn/installed/x64-windows")
  endif()
endif()

if(MSVC)
  list(APPEND CMAKE_IGNORE_PREFIX_PATH
    "C:/ProgramData/anaconda3"
    "C:/ProgramData/anaconda3/Library"
    "C:/tools/msys64/mingw64"
    "C:/tools/msys64/mingw64/lib"
    "C:/tools/msys64/mingw64/lib/cmake"
    "C:/tools/msys64/mingw64/lib/cmake/vtk"
  )
  list(PREPEND CMAKE_PREFIX_PATH "${FLUENTCFF_MSVC_VCPKG_ROOT}")
endif()

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)

if(EXISTS "${_FLUENTCFF_PYBIND11_CMAKE}/pybind11Config.cmake")
  set(_PYBIND11_CMAKE_DIR "${_FLUENTCFF_PYBIND11_CMAKE}")
else()
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE _PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _PYBIND11_CMAKE_DIR_RC
  )
  if(NOT _PYBIND11_CMAKE_DIR_RC EQUAL 0 OR _PYBIND11_CMAKE_DIR STREQUAL "")
    message(FATAL_ERROR "Failed to locate pybind11 CMake dir from .venv or `${Python_EXECUTABLE} -m pybind11 --cmakedir`")
  endif()
endif()
list(PREPEND CMAKE_PREFIX_PATH "${_PYBIND11_CMAKE_DIR}")
find_package(pybind11 CONFIG REQUIRED)

if(MSVC)
  find_package(hdf5 CONFIG REQUIRED)
  if(TARGET hdf5::hdf5-static)
    set(_FLUENTCFF_HDF5_LINK hdf5::hdf5-static)
  elseif(TARGET hdf5::hdf5-shared)
    set(_FLUENTCFF_HDF5_LINK hdf5::hdf5-shared)
  elseif(TARGET hdf5::hdf5)
    set(_FLUENTCFF_HDF5_LINK hdf5::hdf5)
  else()
    message(FATAL_ERROR "hdf5 package found but no known target")
  endif()
else()
  find_package(HDF5 REQUIRED COMPONENTS C)
  set(_FLUENTCFF_HDF5_LINK HDF5::HDF5)
endif()

find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  CommonExecutionModel
  FiltersCore
)

add_library(LocalFLUENTCFFReader STATIC
  "${SOURCE_ROOT}/vendor/vtk/IO/FLUENTCFF/vtkFLUENTCFFInternal.cxx"
  "${SOURCE_ROOT}/vendor/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx"
  "${SOURCE_ROOT}/vendor/cmake/FluentCFFGNNPy/vtkAOSDataArrayTemplateInstantiate.cxx"
)
target_compile_definitions(LocalFLUENTCFFReader PRIVATE
  VTK_USE_EXTERN_TEMPLATE=0
  VTKCOMMONCORE_STATIC_DEFINE
)
target_include_directories(LocalFLUENTCFFReader PUBLIC
  "${SOURCE_ROOT}/vendor/vtk/IO/FLUENTCFF"
)
target_link_libraries(LocalFLUENTCFFReader PUBLIC
  ${_FLUENTCFF_HDF5_LINK}
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::CommonExecutionModel
  VTK::FiltersCore
)

pybind11_add_module(vtkIOFLUENTCFF MODULE fluentcff_pybind.cpp)
target_include_directories(vtkIOFLUENTCFF PRIVATE
  "${SOURCE_ROOT}/vendor/vtk/IO/FLUENTCFF"
)
target_compile_definitions(vtkIOFLUENTCFF PRIVATE
  VTK_USE_EXTERN_TEMPLATE=0
  VTKCOMMONCORE_STATIC_DEFINE
)
target_link_libraries(vtkIOFLUENTCFF PRIVATE
  LocalFLUENTCFFReader
)

if(MSVC)
  target_compile_options(LocalFLUENTCFFReader PRIVATE /utf-8)
  target_compile_options(vtkIOFLUENTCFF PRIVATE /utf-8 /bigobj)
endif()
