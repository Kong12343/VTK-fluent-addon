#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
将 .dat.h5 中 results/.../faces/<field> 各 section 的 [minId,maxId]
与 .cas.h5 中 /meshes/1/faces/zoneTopology 的面区 [minId,maxId]、name 做区间相交，
输出：每个 field / section 对应哪些面区 name（与 vtkFLUENTCFFReader 语义一致）。

依赖: pip install h5py
"""

from __future__ import annotations

import argparse
import sys
from typing import Any, Iterator


def split_field_names(field_list: str) -> list[str]:
    """与 vtkFLUENTCFFReader.cxx 中 SplitFieldNames 一致（分号分隔）。"""
    result: list[str] = []
    npos = 0
    length = len(field_list)
    while npos < length:
        nxt = field_list.find(";", npos)
        if nxt == -1:
            if npos < length:
                result.append(field_list[npos:])
            break
        if nxt > npos:
            result.append(field_list[npos:nxt])
        npos = nxt + 1
    return result


def _decode_name(raw: Any) -> str:
    if raw is None:
        return ""
    if isinstance(raw, bytes):
        return raw.decode("utf-8", errors="replace").rstrip("\0")
    if hasattr(raw, "dtype") and getattr(raw.dtype, "kind", None) in ("S", "O"):
        x = raw.tobytes().decode("utf-8", errors="replace") if raw.dtype.kind == "S" else str(raw)
        return str(x).rstrip("\0")
    return str(raw)


def load_face_zones_from_cas(cas_path: str) -> list[dict[str, Any]]:
    import h5py

    zones: list[dict[str, Any]] = []
    with h5py.File(cas_path, "r") as f:
        zt = f["/meshes/1/faces/zoneTopology"]
        nz = zt.attrs["nZones"]
        n_zones = int(nz[()] if getattr(nz, "shape", ()) != () else nz)
        min_id = zt["minId"][:]
        max_id = zt["maxId"][:]
        zone_fluent_id = zt["id"][:]
        zone_names: list[str] = []
        if "name" in zt:
            name_ds = zt["name"]
            raw = name_ds[()]
            if name_ds.shape == ():
                zone_names = split_field_names(_decode_name(raw))
            else:
                for i in range(len(raw)):
                    zone_names.append(_decode_name(raw[i]).strip())
            # 有时整表 name 挤在第一条里，与 reader 的 SplitFieldNames 对齐
            if len(zone_names) < n_zones and len(zone_names) == 1 and ";" in zone_names[0]:
                zone_names = split_field_names(zone_names[0])
        for i in range(n_zones):
            zid = int(zone_fluent_id[i])
            name = zone_names[i] if i < len(zone_names) else f"zone_{zid}"
            zones.append(
                {
                    "zone_index": i,
                    "name": name,
                    "fluent_zone_id": zid,
                    "min_id": int(min_id[i]),
                    "max_id": int(max_id[i]),
                }
            )
    return zones


def intervals_overlap(a_lo: int, a_hi: int, b_lo: int, b_hi: int) -> bool:
    return max(a_lo, b_lo) <= min(a_hi, b_hi)


def iter_phases(f: Any) -> Iterator[str]:
    r1 = f["/results/1"]
    for k in sorted(r1.keys()):
        if k.startswith("phase-"):
            yield k


def list_face_field_groups(faces_grp: Any) -> list[str]:
    import h5py

    out: list[str] = []
    for key in faces_grp.keys():
        if key == "fields":
            continue
        obj = faces_grp[key]
        if isinstance(obj, h5py.Group) and "nSections" in obj.attrs:
            out.append(key)
    return sorted(out)


def read_field_sections(faces_grp: Any, field_name: str) -> list[tuple[int, int]]:
    import h5py

    g = faces_grp[field_name]
    if not isinstance(g, h5py.Group):
        return []
    ns = g.attrs["nSections"]
    n_sec = int(ns[()] if getattr(ns, "shape", ()) != () else ns)
    sections: list[tuple[int, int]] = []
    for s in range(1, n_sec + 1):
        ds = g[str(s)]
        a, b = ds.attrs["minId"], ds.attrs["maxId"]
        lo = int(a[()] if getattr(a, "shape", ()) != () else a)
        hi = int(b[()] if getattr(b, "shape", ()) != () else b)
        sections.append((lo, hi))
    return sections


def run(cas_path: str, dat_path: str, phase_filter: str | None, field_filter: str | None) -> None:
    import h5py

    zones = load_face_zones_from_cas(cas_path)
    print(f"# cas: {cas_path}")
    print(f"# dat: {dat_path}")
    print(f"# face zones (zoneTopology): {len(zones)}")
    for z in zones:
        print(
            f"  zone[{z['zone_index']}] name={z['name']!r} fluent_id={z['fluent_zone_id']} "
            f"face_id_range=[{z['min_id']},{z['max_id']}] (1-based closed)"
        )
    print()

    with h5py.File(dat_path, "r") as df:
        phases = [phase_filter] if phase_filter else list(iter_phases(df))
        for phase in phases:
            path = f"/results/1/{phase}/faces"
            if path not in df:
                print(f"# skip missing {path}", file=sys.stderr)
                continue
            faces_grp = df[path]
            fields = list_face_field_groups(faces_grp)
            if field_filter:
                fields = [f for f in fields if f == field_filter]
            print(f"## {phase} / faces ({len(fields)} fields)")
            for fn in fields:
                secs = read_field_sections(faces_grp, fn)
                print(f"### field {fn!r}  nSections={len(secs)}")
                for si, (lo, hi) in enumerate(secs, start=1):
                    hits = [
                        z["name"]
                        for z in zones
                        if intervals_overlap(z["min_id"], z["max_id"], lo, hi)
                    ]
                    print(
                        f"    section {si}: face_id [{lo},{hi}]  -> "
                        f"overlapping zone name(s): {hits if hits else '(none)'}"
                    )
            print()


def main() -> None:
    p = argparse.ArgumentParser(description="Map dat face fields to cas face zone names by id range overlap.")
    p.add_argument("cas_h5", help="Path to .cas.h5")
    p.add_argument("dat_h5", help="Path to .dat.h5")
    p.add_argument("--phase", default=None, help="Only this phase, e.g. phase-1 (default: all phases)")
    p.add_argument("--field", default=None, help="Only this HDF5 field group name (default: all)")
    args = p.parse_args()
    try:
        import h5py  # noqa: F401
    except ImportError:
        print("需要安装 h5py:  pip install h5py", file=sys.stderr)
        sys.exit(1)
    run(args.cas_h5, args.dat_h5, args.phase, args.field)


if __name__ == "__main__":
    main()
