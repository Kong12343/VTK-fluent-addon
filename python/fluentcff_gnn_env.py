"""
Windows DLL search paths for `fluentcff_gnn` native extension (VTK/HDF5/torch).

Aligns with AGENT.md / VS Code tasks: use FLUENTCFF_MSVC_VCPKG_ROOT, fall back to
cmake/FluentCFFGNNPy/CMakePresets.json (fluentcff-gnn-py-msvc), register torch/lib
and vcpkg bin via os.add_dll_directory; prepend torch/lib to PATH.

Call before `import fluentcff_gnn` in standalone scripts (plain PowerShell, no task env).
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path


def msvc_vcpkg_root_from_preset(repo_root: str) -> str | None:
    """When FLUENTCFF_MSVC_VCPKG_ROOT is unset, read CMakePresets.json default."""
    preset = Path(repo_root) / "cmake" / "FluentCFFGNNPy" / "CMakePresets.json"
    try:
        with open(preset, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError):
        return None
    for p in data.get("configurePresets") or []:
        if p.get("name") != "fluentcff-gnn-py-msvc":
            continue
        root = (p.get("cacheVariables") or {}).get("FLUENTCFF_MSVC_VCPKG_ROOT")
        if isinstance(root, str) and root.strip():
            root = root.strip()
            if (Path(root) / "bin").is_dir():
                return root
    return None


def win32_add_extension_dll_dirs(repo_root: str) -> None:
    """Register DLL directories on Windows so fluentcff_gnn.pyd can load VTK/HDF5 deps."""
    if sys.platform != "win32":
        return
    exe_dir = Path(sys.executable).resolve().parent
    torch_lib = exe_dir.parent / "Lib" / "site-packages" / "torch" / "lib"
    if torch_lib.is_dir():
        os.add_dll_directory(str(torch_lib))
        os.environ["PATH"] = str(torch_lib) + os.pathsep + os.environ.get("PATH", "")

    vcpkg_installed = (os.environ.get("FLUENTCFF_MSVC_VCPKG_ROOT") or "").strip()
    if not vcpkg_installed:
        vcpkg_installed = msvc_vcpkg_root_from_preset(repo_root) or ""
    if vcpkg_installed:
        bin_dir = Path(vcpkg_installed) / "bin"
        if bin_dir.is_dir():
            os.add_dll_directory(str(bin_dir))

    cuda = os.environ.get("CUDA_PATH") or os.environ.get("CUDA_HOME")
    if cuda:
        cuda_bin = Path(cuda) / "bin"
        if cuda_bin.is_dir():
            os.add_dll_directory(str(cuda_bin))
