"""
Build fixed-width node features and `torch_geometric.data.Data` from `FluentCFFGNNDataset` items.

Boundary row layout: coords(3) | normals(3) | boundary_labels(C_pad) | boundary_fields(K_pad).
Internal row layout: internal_coords(3) | cell_fields(K_pad).

`FeatureLayout` fixes (C_pad, K_pad) across samples; shorter tensors are right-padded with zeros.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import torch


def _require_pyg_data():
    try:
        from torch_geometric.data import Data
    except ImportError as e:
        raise ImportError(
            "torch_geometric is required. Install torch first, then: pip install torch-geometric "
            "(and matching pyg wheels per doc/FluentCFFGNNPy-build-troubleshooting.md section 14)."
        ) from e
    return Data


@dataclass(frozen=True)
class FeatureLayout:
    """Padded channel counts for cross-cas consistency."""

    num_boundary_label_cols: int
    num_field_cols: int

    @property
    def boundary_feature_dim(self) -> int:
        return 3 + 3 + self.num_boundary_label_cols + self.num_field_cols

    @property
    def internal_input_dim(self) -> int:
        return 3 + self.num_field_cols


def infer_layout_from_sample(sample: dict[str, Any]) -> FeatureLayout:
    """Use the sample's native C and K (no padding metadata)."""
    tb = sample["topo_boundary"]
    bf = sample["boundary_fields"]["values"]
    _, c = tb["boundary_labels"].shape
    _, k = bf.shape
    return FeatureLayout(num_boundary_label_cols=int(c), num_field_cols=int(k))


def infer_layout_max(samples: list[dict[str, Any]]) -> FeatureLayout:
    max_c = 0
    max_k = 0
    for s in samples:
        lay = infer_layout_from_sample(s)
        max_c = max(max_c, lay.num_boundary_label_cols)
        max_k = max(max_k, lay.num_field_cols)
    return FeatureLayout(num_boundary_label_cols=max_c, num_field_cols=max_k)


def _pad2d_right(x: torch.Tensor, target_cols: int) -> torch.Tensor:
    """Pad N×c tensor to N×target_cols along dim 1."""
    if x.ndim != 2:
        raise ValueError(f"expected 2D tensor, got shape {tuple(x.shape)}")
    n, c = x.shape
    if c > target_cols:
        raise ValueError(f"tensor cols {c} exceeds layout cols {target_cols}")
    if c == target_cols:
        return x
    out = x.new_zeros((n, target_cols))
    out[:, :c] = x
    return out


def build_x_boundary(sample: dict[str, Any], layout: FeatureLayout) -> torch.Tensor:
    tb = sample["topo_boundary"]
    bf = sample["boundary_fields"]["values"]
    coords = tb["boundary_coords"].float()
    normals = tb["boundary_normals"].float()
    labels = tb["boundary_labels"].float()
    fields = bf.float()

    labels_p = _pad2d_right(labels, layout.num_boundary_label_cols)
    fields_p = _pad2d_right(fields, layout.num_field_cols)

    return torch.cat([coords, normals, labels_p, fields_p], dim=1)


def build_x_internal(sample: dict[str, Any], layout: FeatureLayout) -> torch.Tensor:
    ti = sample["topo_internal"]
    cf = sample["cell_fields"]["values"]
    coords = ti["internal_coords"].float()
    fields = cf.float()
    fields_p = _pad2d_right(fields, layout.num_field_cols)
    return torch.cat([coords, fields_p], dim=1)


def build_y_cell_fields(sample: dict[str, Any], layout: FeatureLayout) -> torch.Tensor:
    """Regression targets: K field columns (padded)."""
    cf = sample["cell_fields"]["values"].float()
    return _pad2d_right(cf, layout.num_field_cols)


def build_pyg_data(
    sample: dict[str, Any],
    layout: FeatureLayout,
    *,
    device: torch.device | None = None,
) -> Any:
    """
    One `Data` per (cas, dat): internal nodes only; `y` stores cell field targets.

    Attributes:
      - x: internal node features (M, Fin)
      - edge_index: (2, E), int64
      - y: (M, K) cell fields (padded)
      - pos: duplicate of internal coords (3), for loaders that expect `pos`
    """
    Data = _require_pyg_data()
    ti = sample["topo_internal"]
    edge_index = ti["edge_index"]
    if edge_index.dtype != torch.int64:
        edge_index = edge_index.long()

    x = build_x_internal(sample, layout)
    y = build_y_cell_fields(sample, layout)
    pos = ti["internal_coords"].float()

    data = Data(x=x, edge_index=edge_index, y=y, pos=pos)
    if device is not None:
        data = data.to(device)
    return data


def boundary_label_valid_mask(sample: dict[str, Any], layout: FeatureLayout) -> torch.Tensor:
    """Per-row boolean mask for label dims that are physical (before padding)."""
    c = sample["topo_boundary"]["boundary_labels"].shape[1]
    m = torch.zeros(layout.num_boundary_label_cols, dtype=torch.bool)
    m[:c] = True
    return m


def field_valid_mask(sample: dict[str, Any], layout: FeatureLayout) -> torch.Tensor:
    """Per-field-column mask before padding."""
    k = sample["boundary_fields"]["values"].shape[1]
    m = torch.zeros(layout.num_field_cols, dtype=torch.bool)
    m[:k] = True
    return m
