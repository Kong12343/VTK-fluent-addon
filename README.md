# GNN-CFD：基于图神经网络的非结构网格流场预测

基于 **VTK + PyTorch Geometric + LibTorch** 的 CFD 非结构网格 GNN 流场预测框架。从 Fluent CFF（`.cas.h5`/`.dat.h5`）中提取网格拓扑与物理场，构建异构图神经网络进行端到端流场预测。

## 项目概览

| 层级 | 组件 | 技术 |
|---|---|---|
| 数据解析 | `vtkFLUENTCFFReader` | C++ / VTK / HDF5 |
| 数据导出 | `FluentCFFGNNExporter` | C++ / LibTorch / pybind11 |
| Python 绑定 | `fluentcff_gnn` 扩展 | pybind11 → `.pyd` |
| 数据集 | `FluentCFFGNNDataset` | PyTorch DataLoader / 磁盘缓存 |
| 特征工程 | `fluentcff_gnn_features` | PyG `Data` / 多维边界对齐 |
| 边界编码 | `fluentcff_boundary_encoder` | PointEmbed → SetAggregate → LatentHead |
| 内部 GNN | `fluentcff_internal_gnn` | GraphSAGE / SAGEConv |
| 训练入口 | `train_baseline_graphsaint` | GraphSAINT / NeighborLoader |
| 可视化 | `FluentCFFZoneViewer` | Qt + VTK GUI |

**核心流程：** Fluent CFF 文件 → `vtkFLUENTCFFReader` 解析拓扑/场量 → `FluentCFFGNNExporter` 导出 Tensor → Python 缓存分片 → 边界编码器 + 内部 GNN 联合训练

**架构愿景：** 端到端 AI 驱动的非结构网格生成与流场预测——体积折叠网络（Volumetric FoldingNet）生成宏观坐标 → 神经连边器（Neural Edge Predictor）修剪拓扑 → 异构流场 GNN（MeshGraphNets）进行物理场扩散求解。详见 [总观.md](总观.md)。

## 快速开始

### 环境要求

- **Windows** + MSVC + Ninja
- **VTK / HDF5**：通过 vcpkg 安装（`x64-windows`）
- **Python 3.13**：`.venv` 虚拟环境
- **PyTorch**：2.11.0+cu128（CUDA 12.8）
- **pybind11**：`pip install pybind11`
- **LibTorch**：随 pip torch 提供

### 构建 GNN Python 扩展

```powershell
# 设定 vcpkg 前缀
$env:FLUENTCFF_MSVC_VCPKG_ROOT = "E:/vcpkg-work/fluentcff-gnn/installed/x64-windows"

# 安装 vcpkg 依赖（可选，如需重建）
# cmake/FluentCFFGNNPy/install-vcpkg-deps-E.ps1

# CMake 配置 + 构建（使用 preset）
cmake -S cmake/FluentCFFGNNPy -B build/fluentcff_gnn_py --preset fluentcff-gnn-py-msvc
cmake --build build/fluentcff_gnn_py
```

### 安装依赖

```powershell
pip install -r requirements.txt              # 核心 + 训练依赖
pip install -r python/requirements-train.txt  # 训练可选依赖（torch-sparse 等）
```

### 运行

```powershell
# 冒烟测试
python python/smoke_test_fluentcff_gnn.py

# 启动可视化查看器
examples/FluentCFFZoneViewer/build-debug/FluentCFFZoneViewer.exe
```

## 目录结构

```
gnn/
├── README.md                          # 本文件
├── 总观.md                             # 架构愿景与设计蓝图
├── AGENT.md                           # Agent 约定与配置说明
├── HISTORY.md                         # 开发历史记录
│
├── vtk/IO/FLUENTCFF/                  # C++ Reader 模块
│   ├── vtkFLUENTCFFReader.h/.cxx      # 核心解析器（拓扑 + 场量 + GNN 数据）
│   └── ...
│
├── FluentCFFGNN/                      # C++ GNN 导出器
│   ├── FluentCFFGNNExporter.h/.cxx    # Tensor 零拷贝导出
│
├── examples/FluentCFFZoneViewer/      # Qt + VTK 可视化查看器
│   ├── main.cxx
│   ├── CMakeLists.txt
│   └── perf-opt/                      # 性能优化脚本与基准
│
├── cmake/FluentCFFGNNPy/              # Python 扩展 CMake 构建
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   └── install-vcpkg-deps-E.ps1       # vcpkg 依赖安装脚本
│
├── python/                            # Python 模块与脚本
│   ├── fluentcff_gnn_pybind.cpp       # pybind11 绑定
│   ├── fluentcff_gnn_env.py           # DLL 搜索路径配置
│   ├── fluentcff_gnn_dataset.py       # 数据集 + 磁盘缓存
│   ├── fluentcff_gnn_features.py      # 特征工程 → PyG Data
│   ├── fluentcff_boundary_encoder.py  # 边界编码器三模块
│   ├── fluentcff_internal_gnn.py      # 内部 GNN（GraphSAGE）
│   ├── fluentcff_field_utils.py       # 场量辅助工具
│   ├── smoke_test_fluentcff_gnn.py    # 冒烟测试
│   ├── train_baseline_graphsaint.py   # 训练入口（GraphSAINT 基线）
│   └── requirements-train.txt         # 训练依赖
│
├── test/                              # 测试文件
├── data/v21/                          # 测试数据（v21 版本 CFF 文件）
│
└── docs/                              # 📖 文档
    ├── fluent-cff-dat-cas-structure.md           # CFF 文件格式与 Reader 主流程
    ├── cff-v21-cas-dat-hdf5-field-tree.md        # V21 HDF5 字段树详解
    ├── fluent-cff-modified-modules.md            # VTK 模块改动记录
    ├── fluentcff_gnn_module.md                   # GNN 模块 API 说明
    ├── FluentCFFGNNPy-build-troubleshooting.md   # 构建排障指南
    └── pipelines-backlog.md                      # 未实现管线备忘
```

## 文档索引

| 文档 | 内容 |
|---|---|
| [CFF 文件格式与 Reader 主流程](docs/fluent-cff-dat-cas-structure.md) | 从 reader 主流程出发，串联 case 拓扑 → VTK 单元重建 → dat 字段注入 → face zone 消费数据 |
| [V21 HDF5 字段树](docs/cff-v21-cas-dat-hdf5-field-tree.md) | 基于 `data/v21` 实测样例的 HDF5 树与字段清单，可对照验证手头 `.cas.h5/.dat.h5` |
| [VTK 模块改动记录](docs/fluent-cff-modified-modules.md) | 构建/链接兼容改动与 reader 性能/结构优化归档 |
| [GNN 模块 API](docs/fluentcff_gnn_module.md) | `fluentcff_gnn` 扩展的完整 API 说明（Exporter、Reader、Dataset、特征工程） |
| [构建排障](docs/FluentCFFGNNPy-build-troubleshooting.md) | MSVC + vcpkg + CUDA + pybind11 环境下常见编译/链接/DLL 问题 |
| [管线 backlog](docs/pipelines-backlog.md) | 尚未实现的管线备忘与迭代优先级 |

**推荐阅读顺序：** 文件格式 → 字段树 → 模块改动 → GNN API → 构建排障 → backlog
