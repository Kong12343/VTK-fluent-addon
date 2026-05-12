from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from setuptools import Extension, find_packages, setup
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name: str, sourcedir: str = "") -> None:
        super().__init__(name, sources=[])
        self.sourcedir = str(Path(sourcedir).resolve())


class CMakeBuild(build_ext):
    def build_extension(self, ext: CMakeExtension) -> None:
        ext_fullpath = Path(self.get_ext_fullpath(ext.name)).resolve()
        extdir = ext_fullpath.parent
        cfg = "Release"

        build_temp = Path(self.build_temp) / ext.name
        build_temp.mkdir(parents=True, exist_ok=True)

        package_root = Path(__file__).resolve().parent.as_posix()
        vcpkg_root = os.environ.get(
            "FLUENTCFF_MSVC_VCPKG_ROOT",
            "E:/vcpkg-work/fluentcff-gnn/installed/x64-windows",
        )
        cmake_exe = os.environ.get("CMAKE_EXECUTABLE", "cmake").strip()
        ninja_exe = os.environ.get("CMAKE_MAKE_PROGRAM")
        if ninja_exe:
            ninja_exe = ninja_exe.strip()
        generator = os.environ.get("CMAKE_GENERATOR")
        if generator:
            generator = generator.strip()
        else:
            generator = "NMake Makefiles" if sys.platform.startswith("win") else "Ninja"

        cmake_args = [
            f"-S{ext.sourcedir}",
            f"-B{build_temp}",
            f"-G{generator}",
            f"-DCMAKE_BUILD_TYPE={cfg}",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{cfg.upper()}={extdir}",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DFLUENTCFF_MSVC_VCPKG_ROOT={vcpkg_root}",
            f"-DSOURCE_ROOT={package_root}",
        ]
        if ninja_exe and "Ninja" in generator:
            cmake_args.append(f"-DCMAKE_MAKE_PROGRAM={ninja_exe}")
        if sys.platform.startswith("win"):
            cmake_args.extend(
                [
                    "-DCMAKE_C_COMPILER=cl",
                    "-DCMAKE_CXX_COMPILER=cl",
                ]
            )

        subprocess.run([cmake_exe, *cmake_args], check=True)
        subprocess.run([cmake_exe, "--build", str(build_temp), "--config", cfg], check=True)


setup(
    name="vtk-addon",
    version="0.1.0",
    description="VTK addon: replace vtkmodules.vtkIOFLUENTCFF with local FLUENTCFF sources",
    install_requires=["vtk"],
    packages=find_packages(include=["vtk_addon_docs", "vtk_addon_docs.*"]),
    package_data={"vtk_addon_docs": ["*.md", "*.pyi"]},
    ext_modules=[CMakeExtension("vtkmodules.vtkIOFLUENTCFF", sourcedir="cpp")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
)
