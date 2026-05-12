"""
Internal mesh GNN with optional boundary latent conditioning (concat on first layer).
Baseline: GraphSAGE message passing + field regression head.
"""

from __future__ import annotations

import torch
import torch.nn as nn

try:
    from torch_geometric.nn import SAGEConv
except ImportError as _e:
    SAGEConv = None  # type: ignore[misc, assignment]
    _IMPORT_ERROR = _e
else:
    _IMPORT_ERROR = None


class InternalFieldGNN(nn.Module):
    """
    Args:
        internal_input_dim: `FeatureLayout.internal_input_dim` (coords + field channels).
        d_z: boundary latent dimension (concat to every node before message passing).
        hidden: hidden width.
        num_layers: number of SAGEConv layers.
        num_field_outputs: K (padded field columns).
    """

    def __init__(
        self,
        internal_input_dim: int,
        d_z: int,
        hidden: int,
        num_layers: int,
        num_field_outputs: int,
    ):
        if SAGEConv is None:
            raise ImportError(
                "torch_geometric is required for InternalFieldGNN. "
                "Install per docs/FluentCFFGNNPy-build-troubleshooting.md section 14."
            ) from _IMPORT_ERROR
        super().__init__()
        self.internal_input_dim = internal_input_dim
        self.d_z = d_z
        fin = internal_input_dim + d_z
        self.input_lin = nn.Linear(fin, hidden)
        self.convs = nn.ModuleList()
        for _ in range(num_layers):
            self.convs.append(SAGEConv(hidden, hidden))
        self.head = nn.Linear(hidden, num_field_outputs)

    def forward(
        self,
        x_internal: torch.Tensor,
        edge_index: torch.Tensor,
        z: torch.Tensor | None,
        *,
        edge_weight: torch.Tensor | None = None,
    ) -> torch.Tensor:
        if z is None:
            z = x_internal.new_zeros(self.d_z)
        if z.dim() != 1 or z.shape[0] != self.d_z:
            raise ValueError(f"z must be shape ({self.d_z},), got {tuple(z.shape)}")
        xz = torch.cat([x_internal, z.unsqueeze(0).expand(x_internal.size(0), -1)], dim=-1)
        h = self.input_lin(xz).relu()
        for conv in self.convs:
            h = conv(h, edge_index, edge_weight=edge_weight).relu()
        return self.head(h)
