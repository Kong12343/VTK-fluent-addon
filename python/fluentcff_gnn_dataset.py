"""
Fluent CFF PyTorch Dataset with four-shard disk cache (boundary/internal topology + fields)
and manifest.json for one cas_key -> many dat_key relationships.
"""

from __future__ import annotations

import hashlib
import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Literal, Sequence

import torch
from torch.utils.data import Dataset

MANIFEST_VERSION = 1
CACHE_PAYLOAD_VERSION = 1


def _norm_path(p: str) -> str:
    return os.path.normcase(os.path.abspath(os.path.realpath(p)))


def _stat_sig(path: str) -> tuple[int, int]:
    st = os.stat(path)
    return (int(st.st_mtime_ns), int(st.st_size))


def _sha16(*parts: bytes) -> str:
    h = hashlib.sha256()
    for b in parts:
        h.update(b)
    return h.hexdigest()[:16]


def compute_cas_key(cas_path: str) -> str:
    """Short hash for topology shards (cas mesh identity + mtime/size)."""
    p = _norm_path(cas_path)
    mt, sz = _stat_sig(p)
    blob = json.dumps(
        {"cas": p, "mtime_ns": mt, "size": sz, "topo_schema": CACHE_PAYLOAD_VERSION},
        sort_keys=True,
    ).encode("utf-8")
    return _sha16(blob)


def compute_dat_key(
    dat_path: str,
    *,
    rename_arrays: bool,
    excluded_field_array_names: Sequence[str],
    max_exported_field_columns: int,
) -> str:
    """Short hash for field shards."""
    p = _norm_path(dat_path)
    mt, sz = _stat_sig(p)
    blob = json.dumps(
        {
            "dat": p,
            "mtime_ns": mt,
            "size": sz,
            "rename_arrays": rename_arrays,
            "excluded": sorted(excluded_field_array_names),
            "max_exported_field_columns": max_exported_field_columns,
            "field_schema": CACHE_PAYLOAD_VERSION,
        },
        sort_keys=True,
    ).encode("utf-8")
    return _sha16(blob)


@dataclass
class FluentCFFSample:
    cas_path: str
    dat_path: str


@dataclass
class ExporterExportConfig:
    rename_arrays: bool = False
    excluded_field_array_names: list[str] = field(default_factory=list)
    max_exported_field_columns: int = 0


LoadWhich = Literal["all", "boundary_only", "internal_only"]


def default_cache_dir() -> str:
    root = os.environ.get("FLUENTCFF_CACHE_DIR") or ""
    if root.strip():
        return os.path.abspath(root.strip())
    return str(Path(__file__).resolve().parent / ".fluentcff_cache")


def _shard_paths(cache_root: str, cas_key: str, dat_key: str) -> dict[str, Path]:
    root = Path(cache_root)
    return {
        "topo_boundary": root / "topo_boundary" / f"{cas_key}.pt",
        "topo_internal": root / "topo_internal" / f"{cas_key}.pt",
        "fields_boundary": root / "fields_boundary" / f"{dat_key}.pt",
        "fields_cell": root / "fields_cell" / f"{dat_key}.pt",
        "manifest": root / "manifest.json",
    }


def _meta_topo_matches(
    meta: dict[str, Any],
    *,
    cas_path: str,
    cas_key: str,
) -> bool:
    if meta.get("payload_version") != CACHE_PAYLOAD_VERSION:
        return False
    if meta.get("cas_key") != cas_key:
        return False
    if _norm_path(meta.get("cas_path") or "") != _norm_path(cas_path):
        return False
    mt, sz = _stat_sig(cas_path)
    if meta.get("mtime_ns") != mt or meta.get("size") != sz:
        return False
    return True


def _meta_fields_matches(
    meta: dict[str, Any],
    *,
    dat_path: str,
    dat_key: str,
    cas_path: str,
    cas_key: str,
    export_cfg: ExporterExportConfig,
) -> bool:
    if meta.get("payload_version") != CACHE_PAYLOAD_VERSION:
        return False
    if meta.get("dat_key") != dat_key:
        return False
    if meta.get("cas_key") != cas_key:
        return False
    if _norm_path(meta.get("dat_path") or "") != _norm_path(dat_path):
        return False
    if _norm_path(meta.get("cas_path") or "") != _norm_path(cas_path):
        return False
    mt, sz = _stat_sig(dat_path)
    if meta.get("mtime_ns") != mt or meta.get("size") != sz:
        return False
    if meta.get("rename_arrays") != export_cfg.rename_arrays:
        return False
    if tuple(meta.get("excluded_field_array_names") or []) != tuple(
        sorted(export_cfg.excluded_field_array_names)
    ):
        return False
    if meta.get("max_exported_field_columns") != export_cfg.max_exported_field_columns:
        return False
    return True


def _torch_save_shard(path: Path, tensors: dict[str, Any], meta: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    blob = dict(tensors)
    blob["meta"] = meta
    torch.save(blob, path)


def _torch_load_shard(path: Path) -> dict[str, Any]:
    try:
        return torch.load(path, map_location="cpu", weights_only=False)
    except TypeError:
        return torch.load(path, map_location="cpu")


def _unlink_if_exists(path: Path) -> None:
    try:
        path.unlink()
    except OSError:
        pass


class FluentCFFGNNDataset(Dataset):
    """
    Each index corresponds to one (cas, dat) pair. Topology shards are shared across rows
    that share the same cas_key (stored once on disk).
    """

    def __init__(
        self,
        samples: Sequence[FluentCFFSample],
        cache_root: str | None = None,
        export_cfg: ExporterExportConfig | None = None,
        *,
        force_rebuild: bool = False,
        load_which: LoadWhich = "all",
        fluentcff_gnn_module: Any | None = None,
    ):
        """
        fluentcff_gnn_module: optional pre-imported module (for tests). Default: import fluentcff_gnn.
        """
        super().__init__()
        self.samples = list(samples)
        self.cache_root = cache_root or default_cache_dir()
        self.export_cfg = export_cfg or ExporterExportConfig()
        self.force_rebuild = force_rebuild
        self.load_which = load_which
        self._fluentcff_gnn = fluentcff_gnn_module

        Path(self.cache_root).mkdir(parents=True, exist_ok=True)

        self._manifest_entries: list[dict[str, Any]] = []
        self._indexed: list[tuple[str, str, str, str]] = []
        # (cas_key, dat_key, cas_path, dat_path) per dataset index

        self._prepare_caches_and_manifest()

    def _import_gnn(self) -> Any:
        if self._fluentcff_gnn is not None:
            return self._fluentcff_gnn
        import fluentcff_gnn  # type: ignore

        return fluentcff_gnn

    def _prepare_caches_and_manifest(self) -> None:
        gnn = self._import_gnn()
        for i, s in enumerate(self.samples):
            cas_path = _norm_path(s.cas_path)
            dat_path = _norm_path(s.dat_path)
            if not os.path.isfile(cas_path):
                raise FileNotFoundError(f"Missing cas: {cas_path}")
            if not os.path.isfile(dat_path):
                raise FileNotFoundError(f"Missing dat: {dat_path}")

            cas_key = compute_cas_key(cas_path)
            dat_key = compute_dat_key(
                dat_path,
                rename_arrays=self.export_cfg.rename_arrays,
                excluded_field_array_names=self.export_cfg.excluded_field_array_names,
                max_exported_field_columns=self.export_cfg.max_exported_field_columns,
            )

            paths = _shard_paths(self.cache_root, cas_key, dat_key)

            need_export = self.force_rebuild or not self._shards_valid(
                paths,
                cas_path=cas_path,
                dat_path=dat_path,
                cas_key=cas_key,
                dat_key=dat_key,
            )

            if need_export:
                self._export_and_write_four_shards(
                    gnn,
                    paths,
                    cas_path=cas_path,
                    dat_path=dat_path,
                    cas_key=cas_key,
                    dat_key=dat_key,
                )

            self._indexed.append((cas_key, dat_key, cas_path, dat_path))
            self._manifest_entries.append(
                {
                    "sample_id": i,
                    "cas_path": cas_path,
                    "dat_path": dat_path,
                    "cas_key": cas_key,
                    "dat_key": dat_key,
                }
            )

        manifest_path = Path(self.cache_root) / "manifest.json"
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(
                {"version": MANIFEST_VERSION, "samples": self._manifest_entries},
                f,
                indent=2,
                ensure_ascii=False,
            )

    def _shards_valid(
        self,
        paths: dict[str, Path],
        *,
        cas_path: str,
        dat_path: str,
        cas_key: str,
        dat_key: str,
    ) -> bool:
        for name in ("topo_boundary", "topo_internal", "fields_boundary", "fields_cell"):
            if not paths[name].is_file():
                return False

        tb = _torch_load_shard(paths["topo_boundary"])
        ti = _torch_load_shard(paths["topo_internal"])
        fb = _torch_load_shard(paths["fields_boundary"])
        fc = _torch_load_shard(paths["fields_cell"])

        if not _meta_topo_matches(tb.get("meta") or {}, cas_path=cas_path, cas_key=cas_key):
            return False
        if not _meta_topo_matches(ti.get("meta") or {}, cas_path=cas_path, cas_key=cas_key):
            return False
        if not _meta_fields_matches(
            fb.get("meta") or {},
            dat_path=dat_path,
            dat_key=dat_key,
            cas_path=cas_path,
            cas_key=cas_key,
            export_cfg=self.export_cfg,
        ):
            return False
        if not _meta_fields_matches(
            fc.get("meta") or {},
            dat_path=dat_path,
            dat_key=dat_key,
            cas_path=cas_path,
            cas_key=cas_key,
            export_cfg=self.export_cfg,
        ):
            return False
        return True

    def _export_and_write_four_shards(
        self,
        gnn: Any,
        paths: dict[str, Path],
        *,
        cas_path: str,
        dat_path: str,
        cas_key: str,
        dat_key: str,
    ) -> None:
        if self.force_rebuild:
            for name in ("topo_boundary", "topo_internal", "fields_boundary", "fields_cell"):
                _unlink_if_exists(paths[name])

        exp = gnn.Exporter()
        exp.SetCaseFileName(cas_path)
        exp.SetDataFileName(dat_path)
        exp.SetRenameArrays(self.export_cfg.rename_arrays)
        exp.EnableAllCellArrays()
        exp.EnableAllFaceArrays()
        exp.ClearExcludedFieldArrayNames()
        if self.export_cfg.excluded_field_array_names:
            exp.SetExcludedFieldArrayNames(list(self.export_cfg.excluded_field_array_names))
        exp.SetMaxExportedFieldColumns(self.export_cfg.max_exported_field_columns)
        exp.Update()

        gdict = exp.ExtractGraphTensors()
        cell = exp.ExtractCellFieldTensor()
        bnd = exp.ExtractBoundaryFieldTensor()

        cas_mt, cas_sz = _stat_sig(cas_path)
        dat_mt, dat_sz = _stat_sig(dat_path)

        meta_topo = {
            "payload_version": CACHE_PAYLOAD_VERSION,
            "cas_key": cas_key,
            "cas_path": cas_path,
            "mtime_ns": cas_mt,
            "size": cas_sz,
            "shard": "topology",
            "subset": "boundary_or_internal_split",
        }
        meta_fields = {
            "payload_version": CACHE_PAYLOAD_VERSION,
            "cas_key": cas_key,
            "cas_path": cas_path,
            "dat_key": dat_key,
            "dat_path": dat_path,
            "mtime_ns": dat_mt,
            "size": dat_sz,
            "rename_arrays": self.export_cfg.rename_arrays,
            "excluded_field_array_names": sorted(self.export_cfg.excluded_field_array_names),
            "max_exported_field_columns": self.export_cfg.max_exported_field_columns,
            "shard": "fields",
        }

        tb_tensors = {
            "boundary_coords": gdict["boundary_coords"],
            "boundary_normals": gdict["boundary_normals"],
            "boundary_labels": gdict["boundary_labels"],
            "zoneType_values": gdict["zoneType_values"],
        }
        tb_meta = dict(meta_topo)
        tb_meta["subset"] = "boundary"

        ti_tensors = {
            "internal_coords": gdict["internal_coords"],
            "edge_index": gdict["edge_index"],
        }
        ti_meta = dict(meta_topo)
        ti_meta["subset"] = "internal"

        fb_tensors = {"values": bnd["values"], "names": list(bnd["names"])}
        fb_meta = dict(meta_fields)
        fb_meta["subset"] = "boundary_fields"

        fc_tensors = {"values": cell["values"], "names": list(cell["names"])}
        fc_meta = dict(meta_fields)
        fc_meta["subset"] = "cell_fields"

        _torch_save_shard(paths["topo_boundary"], tb_tensors, tb_meta)
        _torch_save_shard(paths["topo_internal"], ti_tensors, ti_meta)
        _torch_save_shard(paths["fields_boundary"], fb_tensors, fb_meta)
        _torch_save_shard(paths["fields_cell"], fc_tensors, fc_meta)

    def __len__(self) -> int:
        return len(self._indexed)

    def __getitem__(self, index: int) -> dict[str, Any]:
        cas_key, dat_key, _, _ = self._indexed[index]
        paths = _shard_paths(self.cache_root, cas_key, dat_key)

        out: dict[str, Any] = {
            "sample_id": index,
            "cas_key": cas_key,
            "dat_key": dat_key,
            "meta": self._manifest_entries[index],
        }

        lw = self.load_which
        if lw in ("all", "boundary_only"):
            tb = _torch_load_shard(paths["topo_boundary"])
            out["topo_boundary"] = {k: tb[k] for k in tb if k != "meta"}
            out["topo_boundary_meta"] = tb.get("meta")

            fb = _torch_load_shard(paths["fields_boundary"])
            out["boundary_fields"] = {"values": fb["values"], "names": fb["names"]}
            out["boundary_fields_meta"] = fb.get("meta")

        if lw in ("all", "internal_only"):
            ti = _torch_load_shard(paths["topo_internal"])
            out["topo_internal"] = {k: ti[k] for k in ti if k != "meta"}
            out["topo_internal_meta"] = ti.get("meta")

            fc = _torch_load_shard(paths["fields_cell"])
            out["cell_fields"] = {"values": fc["values"], "names": fc["names"]}
            out["cell_fields_meta"] = fc.get("meta")

        return out


def collate_list_of_dicts(batch: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Default collate: keep list of samples (variable-size graphs)."""
    return batch
