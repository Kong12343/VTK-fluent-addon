"""
Round-trip test: FluentCFFGNNDataset four-shard cache vs direct Exporter export.
Run from repo with .venv Python and built fluentcff_gnn on PYTHONPATH / build/fluentcff_gnn_py.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path


def _setup_paths() -> str:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    _gnn_build = os.path.join(repo_root, "build", "fluentcff_gnn_py")
    if os.path.isdir(_gnn_build):
        sys.path.insert(0, _gnn_build)
    sys.path.insert(0, repo_root)
    sys.path.insert(0, os.path.join(repo_root, "python"))
    return repo_root


def main() -> int:
    repo_root = _setup_paths()
    from fluentcff_gnn_env import win32_add_extension_dll_dirs  # noqa: E402

    win32_add_extension_dll_dirs(repo_root)

    try:
        import torch
    except ImportError:
        print("torch required")
        return 2

    import fluentcff_gnn  # type: ignore
    from fluentcff_gnn_dataset import (
        ExporterExportConfig,
        FluentCFFGNNDataset,
        FluentCFFSample,
        default_cache_dir,
    )

    cas = os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5.cas.h5")
    dat = os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5-x3-0_7.dat.h5")
    if not os.path.isfile(cas) or not os.path.isfile(dat):
        print("SKIP: missing data/v21 sample cas/dat")
        return 0

    cfg = ExporterExportConfig(rename_arrays=False, excluded_field_array_names=[], max_exported_field_columns=0)

    with tempfile.TemporaryDirectory() as tmp:
        ds = FluentCFFGNNDataset(
            [FluentCFFSample(cas, dat)],
            cache_root=tmp,
            export_cfg=cfg,
            force_rebuild=True,
            fluentcff_gnn_module=fluentcff_gnn,
        )
        item = ds[0]

        exp = fluentcff_gnn.Exporter()
        exp.SetCaseFileName(cas)
        exp.SetDataFileName(dat)
        exp.SetRenameArrays(cfg.rename_arrays)
        exp.EnableAllCellArrays()
        exp.EnableAllFaceArrays()
        exp.SetMaxExportedFieldColumns(cfg.max_exported_field_columns)
        exp.Update()
        g = exp.ExtractGraphTensors()
        cell = exp.ExtractCellFieldTensor()
        bnd = exp.ExtractBoundaryFieldTensor()

        assert torch.equal(item["topo_boundary"]["boundary_coords"], g["boundary_coords"])
        assert torch.equal(item["topo_boundary"]["boundary_normals"], g["boundary_normals"])
        assert torch.equal(item["topo_boundary"]["boundary_labels"], g["boundary_labels"])
        assert item["topo_boundary"]["zoneType_values"] == g["zoneType_values"]
        assert torch.equal(item["topo_internal"]["internal_coords"], g["internal_coords"])
        assert torch.equal(item["topo_internal"]["edge_index"], g["edge_index"])
        assert torch.equal(item["boundary_fields"]["values"], bnd["values"])
        assert item["boundary_fields"]["names"] == bnd["names"]
        assert torch.equal(item["cell_fields"]["values"], cell["values"])
        assert item["cell_fields"]["names"] == cell["names"]

        mf = Path(tmp) / "manifest.json"
        assert mf.is_file()
        man = json.loads(mf.read_text(encoding="utf-8"))
        assert man.get("version") == 1
        assert len(man.get("samples") or []) == 1

        ds2 = FluentCFFGNNDataset(
            [FluentCFFSample(cas, dat)],
            cache_root=tmp,
            export_cfg=cfg,
            force_rebuild=False,
            fluentcff_gnn_module=fluentcff_gnn,
        )
        item2 = ds2[0]
        assert torch.equal(item2["topo_internal"]["edge_index"], item["topo_internal"]["edge_index"])

    print("cache roundtrip OK (cache_root default would be", default_cache_dir() + ")")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
