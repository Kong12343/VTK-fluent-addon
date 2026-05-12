"""
Baseline training: boundary encoder + internal GNN with GraphSAINT-style subgraph steps
(or NeighborLoader / random subgraph fallback). One full-graph Data per (cas, dat); batch_size=1 across samples.

Requires: torch, torch-geometric (see python/requirements-train.txt).
"""

from __future__ import annotations

import argparse
import os
import sys
from itertools import islice
from typing import Any, Callable, Iterator
import torch

def _repo_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def _setup_pythonpath(repo_root: str) -> None:
    build = os.path.join(repo_root, "build", "fluentcff_gnn_py")
    if os.path.isdir(build):
        sys.path.insert(0, build)
    sys.path.insert(0, repo_root)
    sys.path.insert(0, os.path.join(repo_root, "python"))


def _try_graph_saint_loader(
    data: Any,
    *,
    batch_size: int,
    sample_coverage: int,
    num_steps: int,
):
    try:
        from torch_geometric.loader import GraphSAINTNodeSampler
        try:
            from torch_geometric.typing import WITH_TORCH_SPARSE
        except ImportError:
            WITH_TORCH_SPARSE = True
    except ImportError:
        return None
    if not WITH_TORCH_SPARSE:
        return None
    try:
        return GraphSAINTNodeSampler(
            data,
            batch_size=batch_size,
            sample_coverage=sample_coverage,
            num_steps=num_steps,
        )
    except Exception:
        return None


def _try_neighbor_loader(
    data: Any,
    *,
    batch_size: int,
    num_neighbors: list[int],
):
    import torch
    try:
        from torch_geometric.loader import NeighborLoader
        try:
            from torch_geometric.typing import WITH_TORCH_SPARSE
        except ImportError:
            WITH_TORCH_SPARSE = True
    except ImportError:
        return None
    if not WITH_TORCH_SPARSE:
        return None
    try:
        dev = data.x.device
        seeds = torch.arange(data.num_nodes, device=dev)
        return NeighborLoader(
            data,
            num_neighbors=num_neighbors,
            batch_size=batch_size,
            shuffle=True,
            input_nodes=seeds,
        )
    except Exception:
        return None


def _random_subgraph_loader(
    data: Any,
    *,
    subgraph_nodes: int,
    steps_per_epoch: int,
    device: torch.device,
) -> Callable[[], Iterator[Any]]:
    """Cheap fallback: random node-induced subgraphs with relabeled edges."""

    import torch
    from torch_geometric.data import Data
    from torch_geometric.utils import subgraph as pyg_subgraph

    def factory() -> Iterator[Any]:
        n = data.num_nodes
        if n <= 0:
            return
        for _ in range(steps_per_epoch):
            k = min(subgraph_nodes, n)
            subset = torch.randperm(n, device=device)[:k]
            edge_index, _ = pyg_subgraph(subset, data.edge_index, relabel_nodes=True, num_nodes=n)
            yield Data(
                x=data.x[subset],
                edge_index=edge_index,
                y=data.y[subset],
                pos=data.pos[subset],
            )

    return factory


def train_one_sample(
    *,
    sample: dict[str, Any],
    layout,
    boundary_encoder,
    internal_gnn,
    optimizer,
    device: torch.device,
    args: argparse.Namespace,
) -> float:
    import torch
    from fluentcff_gnn_features import (
        build_pyg_data,
        build_x_boundary,
        field_valid_mask,
    )

    data = build_pyg_data(sample, layout).to(device)
    x_b = build_x_boundary(sample, layout).to(device)
    field_mask = field_valid_mask(sample, layout).float().to(device)

    loader_or_factory = _try_graph_saint_loader(
        data,
        batch_size=args.saint_batch_size,
        sample_coverage=args.saint_sample_coverage,
        num_steps=args.steps_per_sample,
    )
    strategy = "GraphSAINTNodeSampler"
    if loader_or_factory is None:
        loader_or_factory = _try_neighbor_loader(
            data,
            batch_size=args.neighbor_batch_size,
            num_neighbors=list(map(int, args.neighbor_num_neighbors.split(","))),
        )
        strategy = "NeighborLoader"
    if loader_or_factory is None:
        loader_or_factory = _random_subgraph_loader(
            data,
            subgraph_nodes=args.fallback_subgraph_nodes,
            steps_per_epoch=args.steps_per_sample,
            device=device,
        )
        strategy = "random_subgraph"

    internal_gnn.train()
    boundary_encoder.train()

    losses: list[float] = []

    if strategy == "GraphSAINTNodeSampler":
        batch_iter = loader_or_factory
    elif strategy == "NeighborLoader":
        batch_iter = islice(loader_or_factory, args.steps_per_sample)
    else:
        batch_iter = islice(loader_or_factory(), args.steps_per_sample)

    any_step = False
    z = boundary_encoder(x_b)
    optimizer.zero_grad(set_to_none=True)
    loss_acc = None
    step_count = 0

    for batch in batch_iter:
        any_step = True
        batch = batch.to(device)
        ew = getattr(batch, "edge_weight", None)
        pred = internal_gnn(batch.x, batch.edge_index, z, edge_weight=ew)
        diff = (pred - batch.y) ** 2
        diff = diff * field_mask.unsqueeze(0)
        denom = (field_mask.sum().clamp(min=1.0) * pred.shape[0]).clamp(min=1.0)
        loss = diff.sum() / denom
        loss_acc = loss if loss_acc is None else loss_acc + loss
        step_count += 1
        losses.append(float(loss.detach().cpu()))

    if any_step and loss_acc is not None:
        (loss_acc / step_count).backward()
        optimizer.step()

    if not any_step:
        optimizer.zero_grad(set_to_none=True)
        z = boundary_encoder(x_b)
        ew = getattr(data, "edge_weight", None)
        pred = internal_gnn(data.x, data.edge_index, z, edge_weight=ew)
        diff = (pred - data.y) ** 2 * field_mask.unsqueeze(0)
        denom = (field_mask.sum().clamp(min=1.0) * pred.shape[0]).clamp(min=1.0)
        loss = diff.sum() / denom
        loss.backward()
        optimizer.step()
        losses.append(float(loss.detach().cpu()))
        strategy = "full_graph"

    if args.verbose:
        last = losses[-1] if losses else float("nan")
        print(f"  [{strategy}] micro_steps={len(losses)} loss_last_micro={last:.6g}")

    return sum(losses) / max(len(losses), 1)


def main() -> int:
    repo_root = _repo_root()
    _setup_pythonpath(repo_root)

    try:
        import torch
    except ImportError:
        print("torch required")
        return 2

    try:
        import torch_geometric  # noqa: F401
    except ImportError:
        print(
            "torch_geometric required for this script. Install PyTorch, then:\n"
            "  pip install -r python/requirements-train.txt\n"
            "See docs/FluentCFFGNNPy-build-troubleshooting.md section 14."
        )
        return 2

    from fluentcff_gnn_dataset import (
        ExporterExportConfig,
        FluentCFFGNNDataset,
        FluentCFFSample,
        default_cache_dir,
    )
    from fluentcff_gnn_features import FeatureLayout, infer_layout_max
    from fluentcff_boundary_encoder import BoundaryEncoder
    from fluentcff_internal_gnn import InternalFieldGNN

    try:
        from fluentcff_gnn_env import win32_add_extension_dll_dirs
    except ImportError:
        win32_add_extension_dll_dirs = None  # type: ignore

    parser = argparse.ArgumentParser(description="GraphSAINT-style baseline trainer (path B).")
    parser.add_argument("--cas", type=str, default="", help="Path to .cas.h5")
    parser.add_argument("--dat", type=str, default="", help="Path to .dat.h5")
    parser.add_argument(
        "--cache-dir",
        type=str,
        default="",
        help="FLUENTCFF cache root (default: python/.fluentcff_cache or env FLUENTCFF_CACHE_DIR)",
    )
    parser.add_argument("--epochs", type=int, default=1)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--d-loc", type=int, default=128)
    parser.add_argument("--d-glob", type=int, default=128)
    parser.add_argument("--d-z", type=int, default=64)
    parser.add_argument("--hidden", type=int, default=128)
    parser.add_argument("--gnn-layers", type=int, default=2)
    parser.add_argument("--saint-batch-size", type=int, default=4096)
    parser.add_argument("--saint-sample-coverage", type=int, default=50)
    parser.add_argument("--neighbor-batch-size", type=int, default=4096)
    parser.add_argument("--neighbor-num-neighbors", type=str, default="10,5")
    parser.add_argument("--fallback-subgraph-nodes", type=int, default=4096)
    parser.add_argument("--steps-per-sample", type=int, default=5)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    cas = args.cas.strip() or os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5.cas.h5")
    dat = args.dat.strip() or os.path.join(repo_root, "data", "v21", "step-gamma-20-eta-1_5-x3-0_7.dat.h5")
    if not os.path.isfile(cas) or not os.path.isfile(dat):
        print("SKIP: missing cas/dat; pass --cas and --dat")
        return 0

    if win32_add_extension_dll_dirs is not None:
        win32_add_extension_dll_dirs(repo_root)

    import fluentcff_gnn  # type: ignore

    cache_root = args.cache_dir.strip() or default_cache_dir()
    ds = FluentCFFGNNDataset(
        [FluentCFFSample(cas, dat)],
        cache_root=cache_root,
        export_cfg=ExporterExportConfig(),
        force_rebuild=False,
        fluentcff_gnn_module=fluentcff_gnn,
    )

    samples = [ds[i] for i in range(len(ds))]
    layout: FeatureLayout = infer_layout_max(samples)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    enc = BoundaryEncoder(
        boundary_in_dim=layout.boundary_feature_dim,
        d_loc=args.d_loc,
        d_glob=args.d_glob,
        d_z=args.d_z,
    ).to(device)
    gnn = InternalFieldGNN(
        internal_input_dim=layout.internal_input_dim,
        d_z=args.d_z,
        hidden=args.hidden,
        num_layers=args.gnn_layers,
        num_field_outputs=layout.num_field_cols,
    ).to(device)
    opt = torch.optim.Adam(list(enc.parameters()) + list(gnn.parameters()), lr=args.lr)

    for ep in range(args.epochs):
        loss_m = train_one_sample(
            sample=samples[0],
            layout=layout,
            boundary_encoder=enc,
            internal_gnn=gnn,
            optimizer=opt,
            device=device,
            args=args,
        )
        print(f"epoch {ep + 1}/{args.epochs} mean_loss={loss_m:.6g} device={device}")

    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
