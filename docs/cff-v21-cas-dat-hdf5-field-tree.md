# Fluent CFF V21：`.cas.h5` / `.dat.h5` HDF5 字段与树结构

本文档综合以下来源：

- 项目内说明：`docs/fluent-cff-dat-cas-structure.md`、`h5文件格式.md`
- 本仓库 Reader 实现：`vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx`（实际打开的路径与数据集/属性名）

**说明：** 下文先给出 **通用** CFF 树（与 Reader 一致）；**第 4 节** 为目录 `data/v21` 下实测样例的完整 HDF5 树与 `fields` 清单（2026-04-15 用 `h5py` 遍历生成）。

---

## 项目相关路径（便于对照）

```
gnn/
├── AGENT.md                          # 备注测试数据目录 data\v21
├── data/
│   └── v21/                           # 测试用例（.cas.h5 / .dat.h5）
├── docs/
│   ├── fluent-cff-dat-cas-structure.md
│   └── cff-v21-cas-dat-hdf5-field-tree.md   # 本文件
├── h5文件格式.md                      # V21 简要笔记（settings 等）
└── vtk/IO/FLUENTCFF/
    ├── vtkFLUENTCFFReader.cxx        # HDF5 读取逻辑
    └── vtkFLUENTCFFInternal.h        # 字段显示名映射（非 HDF 路径）
```

---

## 1. `.cas.h5`（Case / 网格）

CFF 判定：`vtkFLUENTCFFReader::OpenCaseFile` 要求同时存在组 `/meshes` 与 `/settings`。

### 1.1 根与元数据

| 路径 | 类型 | 内容（Reader 或笔记） |
|------|------|------------------------|
| `/settings` | group | 必须存在；常见子项见 `h5文件格式.md`（如 `Origin`、`Version` 等），**本 Reader 未读取其中数据集** |
| `/meshes` | group | 网格根 |

### 1.2 `/meshes/1` — 网格实例

**组属性（attribute）：**

| 属性名 | 用途 |
|--------|------|
| `dimension` | 空间维数（`GetDimension`） |
| `nodeOffset` | 节点索引起点相关（`GetNodesGlobal`） |
| `nodeCount` | 节点数量（`GetNodesGlobal`） |
| `cellOffset` / `cellCount` | 单元全局范围（`GetCellsGlobal`） |
| `faceOffset` / `faceCount` | 面全局范围（`GetFacesGlobal`） |

### 1.3 节点 `/meshes/1/nodes`

```
/meshes/1/nodes/
├── zoneTopology/                    # group
│   ├── @nZones                      # attribute
│   ├── minId                        # dataset
│   ├── maxId                        # dataset
│   ├── id                           # dataset
│   ├── name                         # dataset
│   └── dimension                    # dataset
└── coords/                          # group；子组名为 zone id（字符串）
    └── <zoneId>/                    # dataset：节点坐标
        ├── @minId                   # attribute
        └── @maxId                   # attribute
```

### 1.4 单元 `/meshes/1/cells`

```
/meshes/1/cells/
├── zoneTopology/                    # group
│   ├── @nZones
│   ├── minId, maxId, id             # datasets
│   ├── dimension
│   ├── cellType
│   └── childZoneId
├── ctype/                           # group（混合单元类型时）
│   ├── @nSections                   # attribute
│   └── <section>/                   # group，section 为 1,2,...
│       ├── @elementType             # attribute
│       ├── @minId, @maxId           # attributes
│       └── cell-types               # dataset
└── tree/                            # 可选；AMR/树形父单元
    └── 1/                           # group（固定子名 `1`）
        ├── @minId, @maxId
        ├── nkids                    # dataset
        └── kids                     # dataset
```

### 1.5 面 `/meshes/1/faces`

```
/meshes/1/faces/
├── zoneTopology/                    # group
│   ├── @nZones
│   ├── minId, maxId, id, name       # datasets（name 常为分号分隔字符串）
│   ├── dimension
│   ├── zoneType
│   ├── faceType
│   ├── childZoneId
│   ├── shadowZoneId
│   └── flags
├── nodes/                           # group
│   ├── @nSections
│   └── <section>/                   # section = 1,2,...
│       ├── @minId, @maxId
│       ├── nnodes                   # dataset：每面节点数
│       └── nodes                    # dataset：扁平节点 id
├── c0/                              # group：面 → 单元 c0
│   ├── @nSections
│   └── <section>/                   # dataset；@minId, @maxId
├── c1/                              # group：面 → 单元 c1
│   ├── @nSections
│   └── <section>/                   # dataset；@minId, @maxId
├── tree/                            # 可选；面树
│   └── 1/
│       ├── @minId, @maxId
│       ├── nkids
│       └── kids
└── interface/                       # 可选；非一致网格界面
    ├── @nData, @nZones
    ├── nciTopology                  # dataset
    └── <zoneId>/                   # group
        ├── pf0                      # dataset
        └── pf1                      # dataset
```

**Reader 中仍为 TODO、未从文件填充的：** 周期阴影面（`GetPeriodicShadowFaces`）、非一致界面详细信息（`GetNonconformalGridInterfaceFaceInformation`）。

### 1.6 可选：`/special/Overset_DCI`（重叠网格）

`GetCellOverset` 仅检测该路径是否存在；**完整读取逻辑在源码中被注释**，下列树来自注释块中的预期结构：

```
/special/Overset_DCI/
└── cells/
    ├── topology                     # dataset：段索引列表
    └── <section>/                 # group，section 来自 topology[]
        ├── @minId, @maxId
        ├── ndata                    # dataset
        └── data                     # dataset
```

---

## 2. `.dat.h5`（Data / 结果）

存在组 `/results/1`，其下按相（phase）分桶：`phase-1`、`phase-2`、…（`GetMetaData` / `GetData` 循环探测）。

每个 `phase-N` 下与 Reader 相关的固定结构：

```
/results/1/
└── phase-<N>/
    ├── cells/                       # 单元量
    │   ├── fields                   # dataset：字段名列表（分隔规则见实现 `SplitFieldNames`）
    │   └── <fieldName>/             # group：物理量名，如 SV_P、SV_U 等（以文件为准）
    │       ├── @nSections
    │       └── <section>/           # dataset：1, 2, …（分段覆盖不同 id 区间）
    │           ├── @minId          # attribute：全局 cell id 区间（1-based 语义与 case 一致）
    │           ├── @maxId
    │           └── 数据体          # float32 或 float64；形状决定分量数
    └── faces/                       # 面量（结构同 cells）
        ├── fields
        └── <fieldName>/
            ├── @nSections
            └── <section>/
                ├── @minId, @maxId  # 全局 face id 区间
                └── 数据体
```

**要点：**

- `cells` 与 `faces` 下的 **`<fieldName>` 不是固定枚举**，由 Fluent 导出；须读取对应 `fields` 数据集解析。
- 每个字段组有属性 **`nSections`**；每个 section 子数据集名为 **`"1"`、`"2"`、…**，并带 **`minId` / `maxId`**，将值写入按全局 id 对齐的数组（见 `docs/fluent-cff-dat-cas-structure.md` 第 5–8 节）。

### 2.1 常见字段别名（非 HDF 路径）

`vtkFLUENTCFFInternal::FieldsNamesMap`（`vtkFLUENTCFFInternal.h`）在 **`RenameArrays` 开启** 时将缩写映射为可读名（如 `P` → `pressure`）。这不改变 HDF5 中的原始 `<fieldName>`，仅影响 VTK 数组显示名。

---

## 3. 与本地测试文件对齐的建议

其他算例可用 HDF5 工具自行核对，例如：

- `h5dump -n 文件.cas.h5` / `h5dump -n 文件.dat.h5`（需本机安装 HDF5）
- 或 Python `h5py` 递归遍历 groups / datasets / attributes

---

## 4. 实测样例：`F:\Users\20968\projects\ai\gnn\data\v21`

| 文件 | 说明 |
|------|------|
| `step-gamma-20-eta-1_5.cas.h5` | 网格（case） |
| `step-gamma-20-eta-1_5-x3-0_7.dat.h5` | 结果（data），与上者配对 |

本样例 **case** 中额外出现（相对第 1 节通用描述）：

- `/meshes/1/cells/partition/`：`partition-ids`（并行分区 id）
- `/meshes/1/cells/zoneTopology/fields`：附加数据集（Reader 未使用）
- `/meshes/1/faces/zoneTopology/`下除通用项外还有 **`c0`、`c1`、`fields`** 数据集（Reader 未按这些重建拓扑）

本样例 **dat** 中除 `phase-1/cells`、`phase-1/faces` 外还有：

- `/results/residuals/phase-1/<方程名>/`：`data`、`iterations`（残差历史；Reader 不读）
- `/settings/`：`Case File`、`Data Variables`、`Origin`、`Solver`、`Version` 等（Reader 不读）

### 4.1 `step-gamma-20-eta-1_5.cas.h5` — HDF5 树（节选，含类型与属性键）

```text
/ (group)
  meshes/ (group)
    1/ (group) attrs=['cellCount', 'cellOffset', 'dimension', 'faceCount', 'faceOffset', 'nodeCount', 'nodeOffset', 'version']
      cells/ (group)
        ctype/ (group) attrs=['nSections']
          1/ (group) attrs=['elementType', 'maxId', 'minId']
            cell-types  dataset shape=(703471,) dtype=int16 attrs=['chunkDim']
          2/ (group) attrs=['elementType', 'maxId', 'minId']
            cell-types  dataset shape=(3716143,) dtype=int16 attrs=['chunkDim']
          3/ (group) attrs=['elementType', 'maxId', 'minId']
            cell-types  dataset shape=(390534,) dtype=int16 attrs=['chunkDim']
        partition/ (group) attrs=['nSections']
          1/ (group) attrs=['maxId', 'minId', 'numPartitions']
            partition-ids  dataset shape=(4810148,) dtype=int32 attrs=['chunkDim']
        zoneTopology/ (group) attrs=['nZones']
          cellType, childZoneId, dimension, fields, id, maxId, minId, name, zoneType  (datasets)
      faces/ (group)
        c0/ (group) attrs=['nSections']
          1  dataset shape=(19806668,) dtype=uint32 attrs=['chunkDim', 'maxId', 'minId']
        c1/ (group) attrs=['nSections']
          1, 2  datasets dtype=uint32 …
        nodes/ (group) attrs=['nSections']
          1/ (group) attrs=['maxId', 'minId']
            nnodes, nodes  (datasets)
        zoneTopology/ (group) attrs=['nZones']
          c0, c1, childZoneId, dimension, faceType, fields, flags, id, maxId, minId, name, shadowZoneId, zoneType  (datasets)
      nodes/ (group)
        coords/ (group)
          1  dataset shape=(10785067, 3) dtype=float64 attrs=['chunkDim', 'maxId', 'minId']
        zoneTopology/ (group) attrs=['nZones']
          dimension, id, maxId, minId  (datasets)
  settings/ (group)
    Cortex Variables, Domain Variables, Origin, Rampant Variables, Solver, TGrid Variables, Thread Variables, Version  (datasets，均带 @version)
```

### 4.2 `step-gamma-20-eta-1_5-x3-0_7.dat.h5` — 流场树 +残差 + settings

```text
/ (group)
  results/ (group) attrs=['version']
    1/ (group) attrs=['version']
      phase-1/ (group) attrs=['phaseId']
        cells/ (group) attrs=['version']
          fields  dataset |S108
          SV_BF_V/, SV_DENSITY/, … /SV_WALL_DIST/  （每组 @nSections + section「1」数据集）
        faces/ (group) attrs=['version']
          fields  dataset |S275
          SV_ARTIFICIAL_WALL_FLAG/, … /SV_WALL_YPLUS_UTAU/  （分段情况见 4.4：1 段或 2 段）
    residuals/ (group) attrs=['version']
      phase-1/ (group) attrs=['phaseId']
        continuity/, energy/, k/, omega/, x-velocity/, y-velocity/, z-velocity/
          data  shape=(500, 4) float64; iterations  shape=(500,) float64
  settings/ (group)
    Case File, Data Variables, Origin, Solver, Version  (datasets)
```

### 4.3 `/results/1/phase-1/cells/fields` 解析结果

下列名称来自数据集 `fields` 的原始字符串（分号分隔，末尾可有分号）：

`SV_BF_V`、`SV_DENSITY`、`SV_DPM_PARTITION`、`SV_H`、`SV_K`、`SV_MU_LAM`、`SV_MU_T`、`SV_O`、`SV_P`、`SV_T`、`SV_U`、`SV_V`、`SV_W`、`SV_WALL_DIST`

| 字段组 | nSections | section 1形状 | dtype |
|--------|-----------|----------------|-------|
| SV_BF_V | 1 | (4810148, 3) | float64 |
| 其余各标量 | 1 | (4810148,) | float64 |

各 section 数据集均带属性 `minId=1`、`maxId=4810148`（与全局 cell 数一致）。

### 4.4 `/results/1/phase-1/faces/fields` 解析结果

`SV_ARTIFICIAL_WALL_FLAG`、`SV_DENSITY`、`SV_DT_BC_SOURCE`、`SV_FLUX`、`SV_H`、`SV_HEAT_FLUX`、`SV_HEAT_FLUX_SENSIBLE`、`SV_K`、`SV_MACH`、`SV_O`、`SV_P`、`SV_RAD_HEAT_FLUX`、`SV_T`、`SV_U`、`SV_V`、`SV_W`、`SV_WALL_PRORUS_ZONE_FORCE_MEAN`、`SV_WALL_SHEAR`、`SV_WALL_T_INNER`、`SV_WALL_V`、`SV_WALL_VV`、`SV_WALL_YPLUS`、`SV_WALL_YPLUS_UTAU`

| 字段组 | nSections | 备注 |
|--------|-----------|------|
| SV_ARTIFICIAL_WALL_FLAG | 1 | section 1：shape (4413,) |
| SV_FLUX | 1 | section 1：shape (19806668,)（全局面） |
| SV_P | 1 | section 1：shape (19806668,) |
| SV_DENSITY, SV_DT_BC_SOURCE, SV_H, SV_HEAT_FLUX, SV_K, SV_O, SV_RAD_HEAT_FLUX, SV_T, SV_U, SV_V, SV_W | 2 | section 1 常为 (11498,)，section 2 为较大区间 |
| SV_HEAT_FLUX_SENSIBLE, SV_MACH | 1 | 仅 section 1 |
| SV_WALL_* 等壁面相关 | 1 | 多为 (179076,) 或 (179076, 3) |

具体 `minId`/`maxId` 以各 section 上属性为准（本文件中间面段与边界面段分段存储）。
