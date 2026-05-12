# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于 **VTK + PyTorch Geometric + LibTorch** 的非结构网格 CFD 流场 GNN 预测框架。从 Fluent CFF（`.cas.h5`/`.dat.h5`）提取网格拓扑与物理场，构建异构图神经网络做端到端流场预测。

## 环境与路径约束

- **不要调用** `C:/ProgramData/anaconda3/` 下的任何库
- **vcpkg 根目录**: `E:/vcpkg-work/fluentcff-gnn/installed/x64-windows`（通过环境变量 `FLUENTCFF_MSVC_VCPKG_ROOT` 或 `.vscode/settings.json` 中的 `fluentcff.gnn.vcpkgRoot` 指定）
- **Python**: `.venv/Scripts/python.exe`（Python 3.13）
- **PyTorch**: 2.11.0+cu128
- **LibTorch**: 随 pip torch 提供，位于 `.venv/Lib/site-packages/torch/`
- **测试数据**: `data/v21/`

## 构建与运行

### 构建 GNN Python 扩展（MSVC + Ninja）

```powershell
$env:FLUENTCFF_MSVC_VCPKG_ROOT = "E:/vcpkg-work/fluentcff-gnn/installed/x64-windows"
cmake -S cmake/FluentCFFGNNPy -B build/fluentcff_gnn_py --preset fluentcff-gnn-py-msvc
cmake --build build/fluentcff_gnn_py
```

> 在 VS Developer PowerShell / x64 Native Tools 中运行。CMakePreset 位于 `cmake/FluentCFFGNNPy/CMakePresets.json`。

### VS Code 任务（替代命令行）

- `FluentCFFGNNPy: Configure+Build (preset)` — 一键配置+构建
- `FluentCFFGNNPy: smoke test (venv)` — 运行冒烟测试（自动注入环境变量）

### 运行测试

```powershell
python python/smoke_test_fluentcff_gnn.py
```

在普通 PowerShell 中直接运行时，`fluentcff_gnn_env.py` 会从 CMakePresets.json 自动读取 vcpkg 路径回退，无需手动设置 `FLUENTCFF_MSVC_VCPKG_ROOT`。

### 安装训练依赖

```powershell
pip install torch-geometric
pip install -r python/requirements-train.txt
```

### 构建 FluentCFFZoneViewer（Qt + VTK 可视化查看器）

```powershell
# 使用 MSYS2 clang 工具链
cmake -S examples/FluentCFFZoneViewer -B examples/FluentCFFZoneViewer/build-msys2-clang \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build examples/FluentCFFZoneViewer/build-msys2-clang -j 8
```

> 注意：构建前需关闭正在运行的 `FluentCFFZoneViewer.exe`，否则链接阶段会 Permission denied。

## 架构层次

### 数据流: Fluent CFF → VTK Reader → Exporter → PyG Dataset → GNN

```
.cas.h5 / .dat.h5
    │
    ▼
vtkFLUENTCFFReader (C++, vtk/IO/FLUENTCFF/)
    解析 HDF5 拓扑（mesh nodes/cells/faces）+ 物理场（data chunks）
    重建 VTK 非结构网格（含 polyhedron 支持）
    计算质心、edge_index、边界面型心/法向
    按 zoneId 提供区间拷贝接口
    │
    ▼
FluentCFFGNNExporter (C++, FluentCFFGNN/)
    通过 LibTorch 零拷贝组装 boundary/cell/edge Tensor
    │
    ▼
fluentcff_gnn pybind11 扩展 (python/fluentcff_gnn_pybind.cpp)
    导出为 .pyd，暴露 Reader + Exporter
    │
    ▼
FluentCFFGNNDataset (python/fluentcff_gnn_dataset.py)
    四分片磁盘缓存（boundary/internal 拓扑 + 场），manifest.json 管理 cas→dat 关系
    │
    ▼
fluentcff_gnn_features (python/fluentcff_gnn_features.py)
    FeatureLayout 固定维度 → PyG Data（boundary: coords+normals+labels+fields, internal: coords+fields）
    │
    ▼
fluentcff_boundary_encoder + fluentcff_internal_gnn
    Boundary: PointEmbed → SetAggregate → LatentHead
    Internal: GraphSAGE (SAGEConv) + field regression head
    │
    ▼
train_baseline_graphsaint.py
    GraphSAINT / NeighborLoader / 随机子图回退
```

### 核心 C++ 模块

| 路径 | 说明 |
|------|------|
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h/.cxx` | HDF5 解析、VTK 网格重建（所有 cell 类型含 polyhedron）、zone 管理、质心/邻接计算、性能优化（内存池/区间拷贝/预分配） |
| `FluentCFFGNN/FluentCFFGNNExporter.h/.cxx` | Tensor 零拷贝导出（boundary coords/normals/labels, internal coords, edge_index, 场量） |
| `examples/FluentCFFZoneViewer/main.cxx` | Qt+VTK 可视化 GUI（Cell zone / Face zone / FaceNormals 三种视图，VBO 渲染，性能日志双写） |

### 核心 Python 模块

| 文件 | 说明 |
|------|------|
| `python/fluentcff_gnn_pybind.cpp` | pybind11 绑定 Reader + Exporter |
| `python/fluentcff_gnn_env.py` | Windows DLL 搜索路径配置（vcpkg/torch/CUDA），所有原生扩展脚本的入口 |
| `python/fluentcff_gnn_dataset.py` | PyTorch Dataset + 四分片磁盘缓存 + manifest |
| `python/fluentcff_gnn_features.py` | `FeatureLayout` 固定维度 + 边界/内部特征拼接 → PyG `Data` |
| `python/fluentcff_boundary_encoder.py` | 边界编码器（PointEmbed → SetAggregate → LatentHead） |
| `python/fluentcff_internal_gnn.py` | 内部 GNN（GraphSAGE SAGEConv + 场回归头），边界隐向量 concat 注入第一层 |
| `python/fluentcff_field_utils.py` | 场量辅助工具 |
| `python/train_baseline_graphsaint.py` | 训练入口，GraphSAINT / NeighborLoader，无 torch-sparse 时回退随机子图 |

### Field exclusion 机制

Reader 支持 `ExcludedFieldArrayNames` — 在 VTK 层跳过 HDF5 chunk 读取（不等解析后过滤），Exporter 同步排除，按交集对齐边界/内部场量字典序排列。最大展开列数 K≤14（可关闭）。

### Zone 数据访问模式

C++ Reader 提供两种数据访问:
- **Span-based**: `GetCellCentroidsByZone(zoneId, out)` / `GetFaceCentroidsByZone(zoneId, out)` — O(1) 区间复制
- **Individual**: `GetCellZoneId(cellId)` / `GetFaceZoneId(faceId)` — 通过映射表查询

`Cell.zone` / `Face.zone` 冗余字段已移除，改用 `CellIdToZoneId` / `FaceIdToZoneId` 映射 + `CellZoneIdToCellSpan` / `FaceZoneIdToBoundarySpan`。

## 文档

- [docs/](docs/) — 技术文档（CFF 文件格式、HDF5 字段树、VTK 模块改动、GNN API、构建排障、管线 backlog）
- [总观.md](总观.md) — 端到端架构愿景（VolumetricFoldingNet → NeuralEdgePredictor → MeshGraphNets）
- [AGENT.md](AGENT.md) — Agent 约定
- [HISTORY.md](HISTORY.md) — 开发历史记录

## 注意事项

- `FluentCFFZoneViewer` 使用 MSYS2/MinGW clang 工具链，与 GNN Python 扩展的 MSVC 工具链不同
- 构建 viewer 时 PATH 需过滤 conda、优先 MSYS2 `mingw64/bin`（`perf-opt/build-debug.ps1` 含 PATH 清洗逻辑）
- `LocalFLUENTCFFReader` 使用 `VTKCOMMONCORE_STATIC_DEFINE`，与 viewer `main.cxx` 的 dllimport 约定需一致
- 每次代码改动后需更新 `docs/` 下对应文档
