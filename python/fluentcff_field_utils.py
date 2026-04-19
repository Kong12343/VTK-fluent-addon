"""Helpers for Fluent CFF / fluentcff_gnn datasets (optional h5py for dat settings)."""

from __future__ import annotations

import os
from typing import Iterable

import torch


def read_dat_case_basename(dat_path: str) -> str:
    """
    Read /settings/Case File from a .dat.h5 and return basename (matches .cas.h5 lookup).
    Requires h5py.
    """
    try:
        import h5py  # type: ignore
    except ImportError as e:
        raise ImportError("read_dat_case_basename requires the h5py package") from e

    with h5py.File(dat_path, "r") as f:
        ds = f["settings"]["Case File"]
        raw = ds[()]
    if isinstance(raw, bytes):
        s = raw.decode("utf-8", errors="replace")
    elif isinstance(raw, memoryview):
        s = bytes(raw).decode("utf-8", errors="replace")
    else:
        s = str(raw)
    return os.path.basename(s.strip())


def maybe_flip_boundary_normals(normals: torch.Tensor, *, flip: bool = True) -> torch.Tensor:
    """If flip is True, negate normals (e.g. inward vs outward convention)."""
    return -normals if flip else normals


def assert_expanded_field_columns_within_limit(names: Iterable[str], max_k: int = 14) -> None:
    """Raise ValueError if len(list(names)) > max_k."""
    n = len(list(names))
    if n > max_k:
        raise ValueError(f"expanded field column count K={n} exceeds max_k={max_k}")
