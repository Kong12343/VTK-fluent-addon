"""
Boundary encoder as three modules: point embedding, set aggregation, latent head.

See `docs/fluentcff_gnn_module.md` (boundary three modules + GraphSAINT).
"""

from __future__ import annotations

import torch
import torch.nn as nn
class BoundaryPointEmbed(nn.Module):
    """Per-boundary-face linear embed: ℝ^F → ℝ^{d_loc}."""

    def __init__(self, in_dim: int, d_loc: int):
        super().__init__()
        self.lin = nn.Linear(in_dim, d_loc)

    def forward(self, x_boundary: torch.Tensor) -> torch.Tensor:
        return self.lin(x_boundary)


class BoundarySetAggregate(nn.Module):
    """
    Global boundary vector: sum and mean over N, concat → ℝ^{2·d_loc}, then Linear → ℝ^{d_glob}.
    """

    def __init__(self, d_loc: int, d_glob: int):
        super().__init__()
        self.proj = nn.Linear(2 * d_loc, d_glob)

    def forward(self, h_points: torch.Tensor) -> torch.Tensor:
        # h_points: N × d_loc
        s = h_points.sum(dim=0, keepdim=True)
        m = h_points.mean(dim=0, keepdim=True)
        stat = torch.cat([s, m], dim=1)
        return self.proj(stat)


class BoundaryLatentHead(nn.Module):
    """ℝ^{d_glob} → ℝ^{d_z} condition vector."""

    def __init__(self, d_glob: int, d_z: int):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(d_glob, d_glob),
            nn.ReLU(inplace=True),
            nn.Linear(d_glob, d_z),
        )

    def forward(self, g: torch.Tensor) -> torch.Tensor:
        z = self.net(g)
        return z.squeeze(0)


class BoundaryEncoder(nn.Module):
    """Compose A → B → C; forward returns z ∈ ℝ^{d_z}."""

    def __init__(
        self,
        boundary_in_dim: int,
        *,
        d_loc: int = 128,
        d_glob: int = 128,
        d_z: int = 64,
    ):
        super().__init__()
        self.embed = BoundaryPointEmbed(boundary_in_dim, d_loc)
        self.aggregate = BoundarySetAggregate(d_loc, d_glob)
        self.head = BoundaryLatentHead(d_glob, d_z)
        self.d_z = d_z

    def forward(self, x_boundary: torch.Tensor) -> torch.Tensor:
        h = self.embed(x_boundary)
        g = self.aggregate(h)
        z = self.head(g)
        return z


def boundary_encoder_loss_l2(z: torch.Tensor) -> torch.Tensor:
    """Optional regulariser on latent magnitude (baseline stub)."""
    return z.pow(2).mean()
