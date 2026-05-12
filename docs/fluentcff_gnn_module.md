# 模块 `fluentcff_gnn`（`fluentcff_gnn.cp313-win_amd64.pyd` 等）API 说明

本文档描述由 [`python/fluentcff_gnn_pybind.cpp`](../python/fluentcff_gnn_pybind.cpp) 导出的 Python API。绑定底层使用 **VTK**（Fluent CFF reader）与 **LibTorch**（`torch.Tensor`）。

## 产物文件名与导入名

- **Python 导入名**：`fluentcff_gnn`（与 `PYBIND11_MODULE` 一致）。
- **Windows 扩展文件名**：`fluentcff_gnn.cp<XY>-win_amd64.pyd`，其中 **`<XY>` 为解释器次版本号**（例如 CPython **3.13** → `cp313`）。若升级 Python 或更换 venv，需**重新 CMake 构建**；文档标题中的 `cp313` 仅为示例。
- **构建输出目录**：默认在 CMake 二进制目录（例如 `build/fluentcff_gnn_py/`）。运行前需将该目录加入 **`sys.path`**，或安装到 site-packages（本仓库当前以源码树 + build 目录为主）。

## 依赖与运行环境（摘要）

- **PyTorch**：返回值为 `torch.Tensor` 的方法需要已安装 `torch`。
- **原生 DLL（Windows）**：除 `torch` 外，通常需要 **VTK / HDF5**（vcpkg `bin`）及 **CUDA**（若使用 CUDA wheel）在加载路径中。推荐在导入前使用 `os.add_dll_directory`（参见 [`python/smoke_test_fluentcff_gnn.py`](../python/smoke_test_fluentcff_gnn.py) 与 [`FluentCFFGNNPy-build-troubleshooting.md`](FluentCFFGNNPy-build-troubleshooting.md)）。

---

## 类 `Exporter`

封装 [`FluentCFFGNNExporter`](../FluentCFFGNN/FluentCFFGNNExporter.h)：设置 Fluent CFF 的 cas/dat、更新管线，并导出图张量与场张量。

### `Exporter()`

构造一个导出器实例。

### `SetCaseFileName(path: str) -> None`

设置 case（网格）文件路径（例如 `.cas.h5`）。

### `SetDataFileName(path: str) -> None`

设置 data（解算数据）文件路径（例如 `.dat.h5`）。

### `SetRenameArrays(flag: bool) -> None`

是否重命名数组（委托内部 `vtkFLUENTCFFReader`，语义与 VTK reader 一致）。

### `EnableAllCellArrays() -> None` / `EnableAllFaceArrays() -> None`

启用全部 cell / face 数组，便于后续场量导出。

### `SetExcludedFieldArrayNames(names: list[str]) -> None` / `ClearExcludedFieldArrayNames() -> None`

在 **`Update()`** 时把排除列表同步到内部 `vtkFLUENTCFFReader`。名字须与 metadata 注册到数组选择器的名称一致（与 reader 读 dat 时的 `selectedName` 相同：含 **`RenameArrays` 映射**；多相时有 **`-phase_N`** 后缀）。对应数组在 **`GetData()` / `ReadDataForType`** 阶段**不会从 HDF5 读取**体数据（未 enable 的 field 不进入 `CellDataChunks`/`FaceDataChunks`）。

### `SetMaxExportedFieldColumns(max_k: int) -> None` / `GetMaxExportedFieldColumns() -> int`

限制 `ExtractCellFieldTensor` / `ExtractBoundaryFieldTensor` 展开后的 **总列数 K**（标量 1 列，矢量按分量展开）。**默认 14**；设为 **0** 表示不做列数校验。超限时 C++ 抛 `std::runtime_error`。

### `Update() -> None`

执行读取与管线更新（内部调用 C++ `FluentCFFGNNExporter::Update()`）。**须**在提取张量前调用。

### `GetReader() -> vtkFLUENTCFFReader`

返回内部 **`vtkFLUENTCFFReader`** 的代理视图。

- **所有权**：Reader 由 `Exporter` 内部的 `vtkSmartPointer` 持有；Python 侧为 **`py::nodelete`** 包装，**不要在 Python 中假定可独立销毁或替换为自行 `New()` 的对象**。
- 可用于调试或调用 reader 上额外已绑定方法（见下文）。

### `ExtractGraphTensors() -> dict`

返回 **图结构相关**张量，键名固定如下：

| 键 | 类型 | 含义（概要） |
|----|------|----------------|
| `boundary_coords` | `torch.Tensor` | 边界面心坐标，`float32` |
| `boundary_normals` | `torch.Tensor` | 边界面法向，`float32` |
| `boundary_labels` | `torch.Tensor` | 边界 zone 类型 one-hot 等，`float32` |
| `zoneType_values` | `list[int]` | 出现的 zone 类型取值列表 |
| `internal_coords` | `torch.Tensor` | 内部单元质心坐标，`float32` |
| `edge_index` | `torch.Tensor` | 边列表 COO，`shape == (2, E)`，`int64` |
| `face_areas` | `torch.Tensor` | 边界面面积，`shape == (M,)`，`float32`，与 `boundary_coords`/`boundary_normals` 行对齐 |
| `cell_face_areas` | `torch.Tensor` | 每条有向边对应面的面积，`shape == (E,)`，`float32`，与 `edge_index` 列对齐 |

具体维度与 `E` 由网格规模决定。

### `ExtractCellFieldTensor() -> dict` / `ExtractBoundaryFieldTensor() -> dict`

返回 **场量**张量字典：

| 键 | 类型 | 含义 |
|----|------|------|
| `values` | `torch.Tensor` | 拼接后的场量，`float32`（行为与 C++ exporter 一致） |
| `names` | `list[str]` | 与 `values` 列对应的名称列表（矢量展开为 `base[0]`…） |

**列顺序约定**：两侧仅保留 **同时在 cell 与 face 结果集中出现的 VTK 数组名**，按 **字典序** 排列后再按 `ExpandComponentNames` 展开；因此 **`ExtractCellFieldTensor` 与 `ExtractBoundaryFieldTensor` 的 `names` 完全一致且列语义对齐**。

---

## 独立脚本中的 Windows DLL 路径 [`python/fluentcff_gnn_env.py`](../python/fluentcff_gnn_env.py)

在**非 VS Code 任务**、**未设置** `FLUENTCFF_MSVC_VCPKG_ROOT` 的 PowerShell 中，在 `import fluentcff_gnn` **之前**调用：

`from fluentcff_gnn_env import win32_add_extension_dll_dirs`  
`win32_add_extension_dll_dirs(<仓库根目录>)`

行为与 [AGENT.md](../AGENT.md) 一致：注册 `torch/lib`、vcpkg `bin`（环境变量或从 `cmake/FluentCFFGNNPy/CMakePresets.json` 的 `fluentcff-gnn-py-msvc` 回读），以及可选的 `CUDA` `bin`。

## Python 辅助 [`python/fluentcff_field_utils.py`](../python/fluentcff_field_utils.py)（可选）

| 函数 | 说明 |
|------|------|
| `read_dat_case_basename(dat_path)` | 读取 `dat.h5` 的 **`/settings/Case File`**，返回 `basename`（需安装 **h5py**） |
| `maybe_flip_boundary_normals(normals, flip=True)` | 按需对法向取反 |
| `assert_expanded_field_columns_within_limit(names, max_k=14)` | Python 侧列数断言（与 Exporter 的 maxK 独立） |

---

## 类 `vtkFLUENTCFFReader`

底层 VTK reader 的 Python 绑定；与独立 VTK Python 包不同，此处类型名为 **`vtkFLUENTCFFReader`**，且为 **非拥有** 引用（见上节 `GetReader`）。

### 文件与选项

| 方法 | 说明 |
|------|------|
| `SetFileName(path: str) -> None` | 设置 case 文件名 |
| `GetFileName() -> str` | 当前 case 文件名 |
| `SetDataFileName(path: str) -> None` | 设置 data 文件名 |
| `GetDataFileName() -> str` | 当前 data 文件名 |
| `SetRenameArrays(flag: bool) -> None` | 设置是否重命名数组 |
| `GetRenameArrays() -> bool` | 查询 `SetRenameArrays` |
| `SetExcludedFieldArrayNames(names: list[str]) -> None` | 与 `Exporter` 相同语义；在 `RequestData` 里 `GetData()` 之前 disable 对应 cell/face 数组 |
| `ClearExcludedFieldArrayNames() -> None` | 清空排除列表 |
| `GetExcludedFieldArrayNames() -> list[str]` | 当前排除列表 |
| `Update() -> None` | 无参数；执行管线更新（绑定为 lambda，避免 MSVC 对重载 `Update` 的二义性） |

### Face zone 元数据

| 方法 | 说明 |
|------|------|
| `GetNumberOfFaceZones() -> int` | Face zone 数量 |
| `GetFaceZoneName(i: int) -> str` | 第 `i` 个 face zone 名称 |
| `GetFaceZoneIdByName(name: str) -> int` | 按名称查 id |
| `GetFaceZoneType(i: int) -> int` | Face zone 类型 |
| `GetCellZoneType(i: int) -> int` | Cell zone 类型 |

### 图几何与张量（返回 `torch.Tensor`）

| 方法 | 返回 shape / dtype | 说明 |
|------|---------------------|------|
| `GetCellCentroidCount() -> int` | — | 单元质心数量 `N` |
| `GetCellCentroids() -> Tensor` | `(N, 3)`, `float32` | 从 reader 内部缓冲区 **拷贝** 为新 tensor |
| `GetEdgeIndex() -> Tensor` | `(2, E)`, `int64` | 由 `GetEdgeIndex(std::vector<int>& src, dst)` 填充后组装；`E = min(len(src), len(dst))` |
| `GetBoundaryFaceCount() -> int` | — | 边界面数量 `M` |
| `GetFaceCentroids() -> Tensor` | `(M, 3)`, `float32` | 边界面心坐标，**拷贝** |
| `GetFaceNormals() -> Tensor` | `(M, 3)`, `float32` | 边界面法向，**拷贝** |
| `GetFaceAreas() -> Tensor` | `(M,)`, `float32` | 边界面多边形面积，与 `GetFaceCentroids`/`GetFaceNormals` 顺序对齐，**拷贝** |
| `GetCellFaceAreas() -> Tensor` | `(E,)`, `float32` | 每条有向边对应面的面积，与 `edge_index` 对齐 |

### 已加载分块信息

| 方法 | 说明 |
|------|------|
| `GetLoadedCellChunkCount() -> int` | 已加载 cell chunk 数 |
| `GetLoadedCellChunkName(i: int) -> str` | Cell chunk 名称 |
| `GetLoadedCellChunkDim(i: int) -> int` | Cell chunk 维数 |
| `GetLoadedFaceChunkCount() -> int` | 已加载 face chunk 数 |
| `GetLoadedFaceChunkName(i: int) -> str` | Face chunk 名称 |
| `GetLoadedFaceChunkDim(i: int) -> int` | Face chunk 维数 |

---

## 推荐调用顺序（`Exporter`）

1. `exp = fluentcff_gnn.Exporter()`
2. `exp.SetCaseFileName(cas_path)`，`exp.SetDataFileName(dat_path)`
3. （可选）`exp.SetRenameArrays(...)`，`exp.EnableAllCellArrays()`，`exp.EnableAllFaceArrays()`；`exp.SetExcludedFieldArrayNames([...])`；`exp.SetMaxExportedFieldColumns(14)` 或 `0` 关闭校验
4. `exp.Update()`
5. `g = exp.ExtractGraphTensors()`，`cell = exp.ExtractCellFieldTensor()`，`bnd = exp.ExtractBoundaryFieldTensor()`

完整可运行示例见 [`python/smoke_test_fluentcff_gnn.py`](../python/smoke_test_fluentcff_gnn.py)。

---

## Dataset、磁盘缓存与 `manifest.json`

模块 [`python/fluentcff_gnn_dataset.py`](../python/fluentcff_gnn_dataset.py) 提供 **`FluentCFFGNNDataset`**（`torch.utils.data.Dataset`），将 Exporter 结果拆成 **四个 `.pt` 分片** 写入缓存目录，并在根目录生成 **`manifest.json`**，记录「一个 **`cas_key`**（拓扑）对应多个 **`dat_key`**（场）」的样本表。

### 缓存目录布局（默认）

根目录由环境变量 **`FLUENTCFF_CACHE_DIR`** 指定；未设置时使用 **`python/.fluentcff_cache`**（相对于仓库内 [`python/fluentcff_gnn_dataset.py`](../python/fluentcff_gnn_dataset.py) 所在目录）。

| 相对路径 | 含义 |
|----------|------|
| `topo_boundary/<cas_key>.pt` | `boundary_coords`、`boundary_normals`、`boundary_labels`、`zoneType_values`、`face_areas` + `meta` |
| `topo_internal/<cas_key>.pt` | `internal_coords`、`edge_index`、`edge_weight` + `meta` |
| `fields_boundary/<dat_key>.pt` | 边界场 `values`、`names` + `meta`（含配对 `cas_key` / `cas_path`） |
| `fields_cell/<dat_key>.pt` | 内部场 `values`、`names` + `meta` |
| `manifest.json` | `samples[]`：`sample_id`、`cas_path`、`dat_path`、`cas_key`、`dat_key` |

同一 **`cas_key`** 在多行样本中复用时，拓扑分片只在磁盘上存 **一份**；每个 **`dat_key`** 对应一对场分片。

### 失效条件

- **拓扑分片**：`cas` 文件 `mtime`/`size` 变化或 `cas_key` 不匹配 → 对该样本重新导出并覆盖对应 `cas_key` 分片。
- **场分片**：`dat` 变化或 `ExporterExportConfig`（`rename_arrays`、`excluded_field_array_names`、`max_exported_field_columns`）变化 → 重建该 `dat_key` 场分片。

### `__getitem__` 返回键（`load_which="all"` 时）

包含 `topo_boundary`、`topo_internal`、`boundary_fields`、`cell_fields`（均为张量字典或 `values`/`names`），以及各 `*_meta` 与 manifest 行信息。大图 **`edge_index`** 可能达 GB 级，建议 **`DataLoader(..., num_workers=0)`** 或谨慎使用多进程以免重复巨块读盘。

一致性自检脚本：[`python/test_fluentcff_cache_roundtrip.py`](../python/test_fluentcff_cache_roundtrip.py)。

---

## 边界编码三模块 + GraphSAINT 基线（Python）

可选训练管线（路径 B：**单样本一张 `Data`**，`batch_size=1` 跨 `(cas, dat)`；子图步内对 `edge_index` 采样）：

| 模块 | 文件 | 说明 |
|------|------|------|
| 特征与张量形状 | [`python/fluentcff_gnn_features.py`](../python/fluentcff_gnn_features.py) | `FeatureLayout` 固定边界 `C_pad`、场 `K_pad`；拼 `x_boundary`、`x_internal`，构造 `torch_geometric.data.Data`（内部节点 `x`、`y` 为 cell 场）。 |
| 边界三子模块 | [`python/fluentcff_boundary_encoder.py`](../python/fluentcff_boundary_encoder.py) | `BoundaryPointEmbed` → `BoundarySetAggregate`（sum/mean concat）→ `BoundaryLatentHead` → `BoundaryEncoder`，输出 **`z ∈ ℝ^{d_z}`**。 |
| 内部 GNN | [`python/fluentcff_internal_gnn.py`](../python/fluentcff_internal_gnn.py) | `InternalFieldGNN`：`z` 与内部节点拼接后多层 **SAGEConv**，回归 **`K` 维场**。 |
| 训练入口 | [`python/train_baseline_graphsaint.py`](../python/train_baseline_graphsaint.py) | 优先 **`GraphSAINTNodeSampler`**（需 `torch-sparse`），否则 **`NeighborLoader`**，再否则 **随机节点子图**；多微步损失 **一次反传** 更新编码器与 GNN。 |

依赖安装见 [`FluentCFFGNNPy-build-troubleshooting.md`](FluentCFFGNNPy-build-troubleshooting.md) **§14** 与 [`python/requirements-train.txt`](../python/requirements-train.txt)。

---

最小 `DataLoader` 示例：

```python
from torch.utils.data import DataLoader
from fluentcff_gnn_dataset import FluentCFFGNNDataset, FluentCFFSample, collate_list_of_dicts

ds = FluentCFFGNNDataset([FluentCFFSample(cas_path, dat_path)], force_rebuild=False)
loader = DataLoader(ds, batch_size=1, shuffle=False, collate_fn=collate_list_of_dicts, num_workers=0)
batch = next(iter(loader))
```

---

## 与编译排障文档的关系

构建扩展、解决链接与 DLL 加载问题，请参阅 [`FluentCFFGNNPy-build-troubleshooting.md`](FluentCFFGNNPy-build-troubleshooting.md)。
