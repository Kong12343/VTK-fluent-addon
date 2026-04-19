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

具体维度与 `E` 由网格规模决定。

### `ExtractCellFieldTensor() -> dict` / `ExtractBoundaryFieldTensor() -> dict`

返回 **场量**张量字典：

| 键 | 类型 | 含义 |
|----|------|------|
| `values` | `torch.Tensor` | 拼接后的场量，`float32`（行为与 C++ exporter 一致） |
| `names` | `list[str]` | 与 `values` 列对应的数组名称列表 |

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
3. （可选）`exp.SetRenameArrays(...)`，`exp.EnableAllCellArrays()`，`exp.EnableAllFaceArrays()`
4. `exp.Update()`
5. `g = exp.ExtractGraphTensors()`，`cell = exp.ExtractCellFieldTensor()`，`bnd = exp.ExtractBoundaryFieldTensor()`

完整可运行示例见 [`python/smoke_test_fluentcff_gnn.py`](../python/smoke_test_fluentcff_gnn.py)。

---

## 与编译排障文档的关系

构建扩展、解决链接与 DLL 加载问题，请参阅 [`FluentCFFGNNPy-build-troubleshooting.md`](FluentCFFGNNPy-build-troubleshooting.md)。
