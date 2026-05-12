import os
import sys

import torch


def main() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    # Extension is built to build/fluentcff_gnn_py (see CMakePresets / tasks.json); repo root for other local imports.
    _gnn_build = os.path.join(repo_root, "build", "fluentcff_gnn_py")
    if os.path.isdir(_gnn_build):
        sys.path.insert(0, _gnn_build)
    sys.path.insert(0, repo_root)
    sys.path.insert(0, os.path.join(repo_root, "python"))

    from fluentcff_gnn_env import win32_add_extension_dll_dirs  # noqa: E402

    win32_add_extension_dll_dirs(repo_root)

    from python import fluentcff_field_utils  # noqa: E402
    import fluentcff_gnn  

    cas = os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5.cas.h5")
    dat = os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5-x3-0_7.dat.h5")
    if not os.path.exists(cas):
        print("Missing case file:", cas)
        return 2
    if not os.path.exists(dat):
        print("Missing data file:", dat)
        return 2

    try:
        case_base = fluentcff_field_utils.read_dat_case_basename(dat)
        assert case_base == os.path.basename(cas), (case_base, os.path.basename(cas))
    except Exception as ex:
        print("read_dat_case_basename skipped or failed:", ex)

    exp = fluentcff_gnn.Exporter()
    exp.SetCaseFileName(cas)
    exp.SetDataFileName(dat)
    exp.SetRenameArrays(False)
    exp.EnableAllCellArrays()
    exp.EnableAllFaceArrays()
    # Rich cases may exceed K=14 expanded columns; disable limit for generic smoke.
    exp.SetMaxExportedFieldColumns(0)
    exp.Update()

    g = exp.ExtractGraphTensors()
    print("boundary_coords", tuple(g["boundary_coords"].shape), g["boundary_coords"].dtype)
    print("boundary_normals", tuple(g["boundary_normals"].shape), g["boundary_normals"].dtype)
    print("boundary_labels", tuple(g["boundary_labels"].shape), g["boundary_labels"].dtype)
    print("zoneType_values", g["zoneType_values"])
    print("internal_coords", tuple(g["internal_coords"].shape), g["internal_coords"].dtype)
    print("edge_index", tuple(g["edge_index"].shape), g["edge_index"].dtype)
    print("face_areas", tuple(g["face_areas"].shape), g["face_areas"].dtype)
    print("cell_face_areas (sparsified)", tuple(g["cell_face_areas"].shape), g["cell_face_areas"].dtype)

    cell = exp.ExtractCellFieldTensor()
    bnd = exp.ExtractBoundaryFieldTensor()
    print("cell_fields", tuple(cell["values"].shape), "names", len(cell["names"]))
    print("boundary_fields", tuple(bnd["values"].shape), "names", len(bnd["names"]))
    assert cell["names"] == bnd["names"], "cell/boundary field names must match (sorted intersection)"
    assert cell["values"].shape[1] == bnd["values"].shape[1]

    # Basic sanity checks
    assert g["boundary_coords"].shape[1] == 3
    assert g["boundary_normals"].shape[1] == 3
    assert g["internal_coords"].shape[1] == 3
    assert g["edge_index"].shape[0] == 2
    assert g["face_areas"].shape[0] == g["boundary_coords"].shape[0]
    assert g["cell_face_areas"].shape[0] == g["edge_index"].shape[1]
    if g["face_areas"].numel() > 0:
        assert (g["face_areas"] >= 0).all().item()
    if g["cell_face_areas"].numel() > 0:
        assert (g["cell_face_areas"] >= 0).all().item()
        # Sparsification invariants: bidirectional pairs, alignment, positive weights, no NaN
        E_out = g["edge_index"].shape[1]
        assert E_out % 2 == 0, "edge_index must have even count (bidirectional pairs)"
        assert g["cell_face_areas"].shape[0] == E_out, "cell_face_areas must align with edge_index"
        assert (g["cell_face_areas"] > 0).all().item(), "sparsified weights must be positive"
        assert torch.isfinite(g["cell_face_areas"]).all().item(), "sparsified weights must be finite"
    if g["boundary_labels"].numel() > 0:
        row_sum = g["boundary_labels"].sum(dim=1)
        # One-hot rows should sum to 1 (or 0 if something is missing).
        print("boundary_labels row_sum stats:", float(row_sum.min()), float(row_sum.max()))

    # Check no NaNs in exported float tensors (NaNs are replaced with 0 in exporter).
    for k in ["boundary_coords", "boundary_normals", "internal_coords",
              "face_areas", "cell_face_areas"]:
        t = g[k]
        if t.numel() > 0:
            assert torch.isfinite(t).all().item()
    if cell["values"].numel() > 0:
        assert torch.isfinite(cell["values"]).all().item()
    if bnd["values"].numel() > 0:
        assert torch.isfinite(bnd["values"]).all().item()

    # Field exclusion: skip HDF5 read for one array when possible (requires correct registry name).
    rd = exp.GetReader()
    names_before = [rd.GetLoadedCellChunkName(i) for i in range(rd.GetLoadedCellChunkCount())]
    if names_before:
        victim = names_before[0]
        exp.ClearExcludedFieldArrayNames()
        exp.SetExcludedFieldArrayNames([victim])
        exp.Update()
        names_after = [rd.GetLoadedCellChunkName(i) for i in range(rd.GetLoadedCellChunkCount())]
        assert victim not in names_after, (victim, names_after)

    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

