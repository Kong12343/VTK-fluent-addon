# vtk-addon

`vtk-addon` provides a replacement build of `vtkmodules.vtkIOFLUENTCFF`
using the local FLUENT CFF reader sources bundled with this package.

## Distribution model

- On supported Windows CPython 3.12 x64 environments, you can install the prebuilt wheel.
- On other platforms or Python/ABI combinations, install from source distribution (`sdist`)
  and let `pip` build locally.

## Installation

### 1. Prebuilt wheel

Use the wheel when all of the following are true:

- Python is CPython 3.12
- Platform is Windows
- Architecture is x86_64 / AMD64

Example:

```powershell
python -m pip install --force-reinstall --no-deps vtk_addon-0.1.0-cp312-cp312-win_amd64.whl
```

### 2. Source build fallback

Use the source package when any of the following are true:

- platform is not `win_amd64`
- Python ABI does not match `cp312`
- you want to build against the local machine's toolchain and VTK/HDF5 development environment

Example:

```powershell
python -m pip install vtk_addon-0.1.0.tar.gz
```

Or from an unpacked source tree:

```powershell
python -m pip install .
```

In this mode, `pip` will compile `vtkmodules.vtkIOFLUENTCFF` locally.

## Source build requirements

This package builds a native extension with CMake and pybind11 and also needs:

- a working C/C++ toolchain
- CMake
- pybind11
- VTK development files discoverable by `find_package(VTK ...)`
- HDF5 development files discoverable by CMake

On Windows with MSVC, you will typically also set:

- `FLUENTCFF_MSVC_VCPKG_ROOT`

pointing to a VTK/HDF5 installation prefix such as a `vcpkg` triplet root.

Example:

```powershell
$env:FLUENTCFF_MSVC_VCPKG_ROOT="E:\vcpkg-work\fluentcff-gnn\installed\x64-windows"
python -m pip install vtk_addon-0.1.0.tar.gz
```

## Installer behavior

When both wheel and source distribution are available through the same package index:

- matching environments should receive the prebuilt wheel automatically
- non-matching environments should fall back to the source distribution and compile locally

This means you do not need a separate package name for the source-build path.

## Included documentation

After installation, package documentation is available in `vtk_addon_docs`, including:

- `vtkIOFLUENTCFF_api_reference.md`
- `vtkIOFLUENTCFF_bindings.pyi`
