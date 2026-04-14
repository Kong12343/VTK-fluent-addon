cmake_minimum_required(VERSION 3.21)
project(FluentCFFZoneViewer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

# MSYS2 Installation path
set(MSYS2_MINGW64_ROOT "C:/tools/msys64/mingw64" CACHE PATH "MSYS2 MinGW64 installation path")

# Set up CMAKE prefix paths
list(PREPEND CMAKE_PREFIX_PATH
  "${MSYS2_MINGW64_ROOT}/lib/cmake"
  "${MSYS2_MINGW64_ROOT}/lib/cmake/qt6"
  "${MSYS2_MINGW64_ROOT}/lib/cmake/vtk"
)

# Set CMake variables for Qt6
set(Qt6_DIR "${MSYS2_MINGW64_ROOT}/lib/cmake/Qt6" CACHE PATH "Qt6 CMake configuration")

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  message(STATUS "This example is intended to be built with clang/clang-cl or g++, current compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# Find Qt6
message(STATUS "Looking for Qt6 in: ${Qt6_DIR}")
find_package(Qt6 6.0 REQUIRED COMPONENTS 
  Core
  Gui
  Widgets
  OpenGLWidgets
)

# Find VTK
message(STATUS "Looking for VTK in CMAKE_PREFIX_PATH")
find_package(VTK REQUIRED COMPONENTS
  CommonColor
  CommonCore
  CommonDataModel
  CommonExecutionModel
  CommonMisc
  FiltersCore
  GUISupportQt
  InteractionStyle
  RenderingCore
  RenderingOpenGL2
  hdf5
)

set(LOCAL_FLUENTCFF_DIR "${CMAKE_CURRENT_LIST_DIR}/../../vtk/IO/FLUENTCFF")

add_library(LocalFLUENTCFFReader STATIC
  "${LOCAL_FLUENTCFF_DIR}/vtkFLUENTCFFInternal.cxx"
  "${LOCAL_FLUENTCFF_DIR}/vtkFLUENTCFFReader.cxx"
  "${CMAKE_CURRENT_LIST_DIR}/vtkAOSDataArrayTemplateInstantiate.cxx"
)

target_include_directories(LocalFLUENTCFFReader 
  PUBLIC 
    "${LOCAL_FLUENTCFF_DIR}"
  PRIVATE 
    "${MSYS2_MINGW64_ROOT}/include"
)

target_compile_options(LocalFLUENTCFFReader PRIVATE -U VTK_USE_EXTERN_TEMPLATE)

target_compile_definitions(LocalFLUENTCFFReader PRIVATE
  VTKCOMMONCORE_STATIC_DEFINE
  VTKCOMMONDATAMODEL_STATIC_DEFINE
)

target_link_libraries(LocalFLUENTCFFReader
  PUBLIC
    VTK::CommonCore
    VTK::CommonExecutionModel
  PRIVATE
    VTK::CommonDataModel
    VTK::CommonMisc
    VTK::hdf5
)

add_executable(FluentCFFZoneViewer main.cxx)

target_link_libraries(FluentCFFZoneViewer
  PRIVATE
    LocalFLUENTCFFReader
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    VTK::CommonColor
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::FiltersCore
    VTK::GUISupportQt
    VTK::InteractionStyle
    VTK::RenderingCore
    VTK::RenderingOpenGL2
)

vtk_module_autoinit(
  TARGETS FluentCFFZoneViewer
  MODULES
    VTK::GUISupportQt
    VTK::InteractionStyle
    VTK::RenderingCore
    VTK::RenderingOpenGL2
)
