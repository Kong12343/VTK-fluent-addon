cmake_minimum_required(VERSION 3.20)
project(FluentCFFZoneViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MSYS2_PREFIX "C:/tools/msys64/mingw64")
set(MSYS2_BIN "${MSYS2_PREFIX}/bin")
if(NOT EXISTS "${MSYS2_BIN}")
  message(FATAL_ERROR "MSYS2 bin directory not found: ${MSYS2_BIN}")
endif()

list(PREPEND CMAKE_PREFIX_PATH
  "${MSYS2_PREFIX}"
  "${MSYS2_PREFIX}/lib/cmake"
  "${MSYS2_PREFIX}/lib/cmake/vtk"
  "${MSYS2_PREFIX}/lib/cmake/nlohmann_json"
)
list(APPEND CMAKE_IGNORE_PREFIX_PATH
  "C:/ProgramData/anaconda3"
  "C:/ProgramData/anaconda3/Library"
)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent OpenGL OpenGLWidgets)
find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  CommonColor
  FiltersCore
  FiltersGeneral
  FiltersSources
  ImagingCore
  InteractionStyle
  InteractionWidgets
  IOImage
  RenderingContext2D
  RenderingCore
  RenderingHyperTreeGrid
  RenderingOpenGL2
  RenderingUI
  GUISupportQt
)

set(HDF5_C_LIB "${MSYS2_PREFIX}/lib/libhdf5.dll.a")
set(HDF5_HL_LIB "${MSYS2_PREFIX}/lib/libhdf5_hl.dll.a")
if(NOT EXISTS "${HDF5_C_LIB}" OR NOT EXISTS "${HDF5_HL_LIB}")
  message(FATAL_ERROR "MSYS2 HDF5 import libraries not found under ${MSYS2_PREFIX}/lib")
endif()

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
    "${HDF5_C_LIB}"
    "${HDF5_HL_LIB}"
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::FiltersCore
    VTK::CommonExecutionModel
)

add_executable(FluentCFFZoneViewer main.cxx)
target_link_libraries(FluentCFFZoneViewer
  PRIVATE
    LocalFLUENTCFFReader
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    VTK::GUISupportQt
    VTK::RenderingOpenGL2
    VTK::RenderingCore
    VTK::InteractionStyle
    VTK::InteractionWidgets
    VTK::CommonColor
)

vtk_module_autoinit(
  TARGETS FluentCFFZoneViewer
  MODULES
    VTK::GUISupportQt
    VTK::RenderingOpenGL2
)

# Keep runtime DLL loading consistent by co-locating dependencies from one MSYS2 bin tree.
set(_runtime_globs
  "Qt6*.dll"
  "libvtk*.dll"
  "libhdf5*.dll"
  "libszip*.dll"
  "zlib*.dll"
  "libcurl*.dll"
  "libssl*.dll"
  "libcrypto*.dll"
  "libgcc_s_seh-1.dll"
  "libstdc++-6.dll"
  "libwinpthread-1.dll"
  "libomp.dll"
  "libtbb12.dll"
)
set(_runtime_dlls "")
foreach(_glob IN LISTS _runtime_globs)
  file(GLOB _found_dlls "${MSYS2_BIN}/${_glob}")
  list(APPEND _runtime_dlls ${_found_dlls})
endforeach()
list(REMOVE_DUPLICATES _runtime_dlls)
foreach(_dll IN LISTS _runtime_dlls)
  add_custom_command(TARGET FluentCFFZoneViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:FluentCFFZoneViewer>"
  )
endforeach()
add_custom_command(TARGET FluentCFFZoneViewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FluentCFFZoneViewer>/platforms"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${MSYS2_BIN}/../share/qt6/plugins/platforms/qwindows.dll"
    "$<TARGET_FILE_DIR:FluentCFFZoneViewer>/platforms/qwindows.dll"
)

set_target_properties(FluentCFFZoneViewer PROPERTIES
  VS_DEBUGGER_ENVIRONMENT "PATH=${MSYS2_BIN};%PATH%"
)
