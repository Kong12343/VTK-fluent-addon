import os
import sys
from pathlib import Path

import torch


def _win32_add_extension_dll_dirs() -> None:
    """Python 3.8+ on Windows: dependent DLL dirs must be registered (PATH alone is unreliable)."""
    if sys.platform != "win32":
        return
    exe_dir = Path(sys.executable).resolve().parent
    torch_lib = exe_dir.parent / "Lib" / "site-packages" / "torch" / "lib"
    if torch_lib.is_dir():
        os.add_dll_directory(str(torch_lib))
    vcpkg_installed = os.environ.get("FLUENTCFF_MSVC_VCPKG_ROOT")
    if vcpkg_installed:
        bin_dir = Path(vcpkg_installed) / "bin"
        if bin_dir.is_dir():
            os.add_dll_directory(str(bin_dir))
    cuda = os.environ.get("CUDA_PATH") or os.environ.get("CUDA_HOME")
    if cuda:
        cuda_bin = Path(cuda) / "bin"
        if cuda_bin.is_dir():
            os.add_dll_directory(str(cuda_bin))


def main() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    # Extension is built to build/fluentcff_gnn_py (see CMakePresets / tasks.json); repo root for other local imports.
    _gnn_build = os.path.join(repo_root, "build", "fluentcff_gnn_py")
    if os.path.isdir(_gnn_build):
        sys.path.insert(0, _gnn_build)
    sys.path.insert(0, repo_root)

    _win32_add_extension_dll_dirs()

    import fluentcff_gnn  # type: ignore

    cas = os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5.cas.h5")
    dat = os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5-x3-0_7.dat.h5")
    if not os.path.exists(cas):
        print("Missing case file:", cas)
        return 2
    if not os.path.exists(dat):
        print("Missing data file:", dat)
        return 2

    exp = fluentcff_gnn.Exporter()
    exp.SetCaseFileName(cas)
    exp.SetDataFileName(dat)
    exp.SetRenameArrays(False)
    exp.EnableAllCellArrays()
    exp.EnableAllFaceArrays()
    exp.Update()

    g = exp.ExtractGraphTensors()
    print("boundary_coords", tuple(g["boundary_coords"].shape), g["boundary_coords"].dtype)
    print("boundary_normals", tuple(g["boundary_normals"].shape), g["boundary_normals"].dtype)
    print("boundary_labels", tuple(g["boundary_labels"].shape), g["boundary_labels"].dtype)
    print("zoneType_values", g["zoneType_values"])
    print("internal_coords", tuple(g["internal_coords"].shape), g["internal_coords"].dtype)
    print("edge_index", tuple(g["edge_index"].shape), g["edge_index"].dtype)

    cell = exp.ExtractCellFieldTensor()
    bnd = exp.ExtractBoundaryFieldTensor()
    print("cell_fields", tuple(cell["values"].shape), "names", len(cell["names"]))
    print("boundary_fields", tuple(bnd["values"].shape), "names", len(bnd["names"]))

    # Basic sanity checks
    assert g["boundary_coords"].shape[1] == 3
    assert g["boundary_normals"].shape[1] == 3
    assert g["internal_coords"].shape[1] == 3
    assert g["edge_index"].shape[0] == 2
    if g["boundary_labels"].numel() > 0:
        row_sum = g["boundary_labels"].sum(dim=1)
        # One-hot rows should sum to 1 (or 0 if something is missing).
        print("boundary_labels row_sum stats:", float(row_sum.min()), float(row_sum.max()))

    # Check no NaNs in exported float tensors (NaNs are replaced with 0 in exporter).
    for k in ["boundary_coords", "boundary_normals", "internal_coords"]:
        t = g[k]
        if t.numel() > 0:
            assert torch.isfinite(t).all().item()
    if cell["values"].numel() > 0:
        assert torch.isfinite(cell["values"]).all().item()
    if bnd["values"].numel() > 0:
        assert torch.isfinite(bnd["values"]).all().item()

    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

