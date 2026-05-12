# Fluent CFF 相关改动模块总结

## 1. 本次改动涉及的模块

这份文档覆盖的是“为让 `FluentCFFZoneViewer` 在当前工具链下可构建、可验证、可持续优化”的一组改动。它们大致可以分为两类：

### 1.1 阶段 A：构建/链接兼容（解决模板符号缺失）

核心目标不是功能扩展，而是：

- 规避 MSYS2 预编译 VTK 下 `vtkAOSDataArrayTemplate<T>::SetValue` 模板符号缺失导致的链接错误
- **保持对外行为与语义一致**（Fluent CFF 的拓扑解释与结果字段含义不变）
- 让 `FluentCFFZoneViewer` 能在当前工具链下完成构建

主要涉及文件：

- [vtkFLUENTCFFReader.cxx](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx)
- [CMakeLists.txt](../examples/FluentCFFZoneViewer/CMakeLists.txt)
- [vtkAOSDataArrayTemplateInstantiate.cxx](../examples/FluentCFFZoneViewer/vtkAOSDataArrayTemplateInstantiate.cxx)

### 1.2 阶段 B：reader 性能/结构优化（减少重复遍历与分配）

在可构建基础上，后续优化主要集中在 `vtkFLUENTCFFReader` 的热路径（解析、拓扑重建、字段注入）上，通过缓存/池化/批量写入等手段降低重复扫描与临时分配，尽量缩短 `Update` 和字段切换耗时。

主要涉及文件（概览）：

- [vtkFLUENTCFFReader.cxx](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx)
- [vtkFLUENTCFFReader.h](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h)
- 性能验证流程（可复现实验/采样脚本）：
  - [perf-opt/README.md](../examples/FluentCFFZoneViewer/perf-opt/README.md)
  - [perf-opt/build-debug.ps1](../examples/FluentCFFZoneViewer/perf-opt/build-debug.ps1)
  - [perf-opt/run-v21.ps1](../examples/FluentCFFZoneViewer/perf-opt/run-v21.ps1)
  - [perf-opt/collect-log.ps1](../examples/FluentCFFZoneViewer/perf-opt/collect-log.ps1)
  - [perf-opt/baseline-notes.md](../examples/FluentCFFZoneViewer/perf-opt/baseline-notes.md)

## 2. `vtkFLUENTCFFReader.cxx` 的改动

### 2.1 改动动机

原始实现里多处使用了：

- `InsertNextValue`
- `InsertNextTuple`
- `SetNumberOfPoints`
- `InsertPoint`

这些 API 在当前 MSYS2/VTK 组合下会间接走到：

```cpp
vtkGenericDataArray<vtkAOSDataArrayTemplate<T>, T>::SetValue(...)
```

最终需要链接到：

```cpp
vtkAOSDataArrayTemplate<T>::SetValue(...)
```

但该符号在当前分发的共享库中没有被正确导出，导致链接失败。

### 2.2 具体调整

为绕开这条模板调用链，`vtkFLUENTCFFReader.cxx` 中把若干数据写入路径改成了“先申请连续缓冲区，再直接写指针”的方式。

主要包括：

- polyhedron face 数据写入 `vtkIdTypeArray`
- cell data 数组写入 `vtkDoubleArray`
- UDM 拆分标量数组写入 `vtkDoubleArray`
- face zone scalar 写入 `vtkDoubleArray`
- node 坐标写入 `vtkPoints` 底层 `vtkDoubleArray`

对应位置示例：

- polyhedron face 数组写入：[vtkFLUENTCFFReader.cxx](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L274)
- cell data 批量写入：[vtkFLUENTCFFReader.cxx](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L309)
- UDM 标量拆分：[vtkFLUENTCFFReader.cxx](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L346)
- face scalar 输出：[vtkFLUENTCFFReader.cxx](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L927)
- points 底层写入：[vtkFLUENTCFFReader.cxx](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1075)

### 2.3 影响

这部分改动：

- 不改变 reader 对外 API
- 不改变拓扑语义
- 不改变 `.cas/.dat` 解释方式
- 只改变 VTK 数组的填充路径

本质上属于“构建兼容性修复 + 内部写入实现替换”。

## 3. `examples/FluentCFFZoneViewer/CMakeLists.txt` 的改动

### 3.1 改动动机

仅修改 reader 源码还不够，因为某些 VTK inline API 依旧会在当前编译单元里生成对：

- `vtkAOSDataArrayTemplate<int>::SetValue`
- `vtkAOSDataArrayTemplate<long long>::SetValue`

的引用。

因此示例工程里需要显式补足模板实例化路径。

### 3.2 具体调整

`LocalFLUENTCFFReader` 这个本地静态库目标增加了：

- 新源文件 `vtkAOSDataArrayTemplateInstantiate.cxx`
- 编译选项 `-U VTK_USE_EXTERN_TEMPLATE`

对应位置：[CMakeLists.txt](../examples/FluentCFFZoneViewer/CMakeLists.txt)

### 3.3 作用

这样做的目的有两个：

1. 避免头文件中的 extern template 声明把实例化责任继续推给系统 VTK 动态库
2. 让示例自己的本地静态库能携带所需模板定义，完成最终链接

## 4. `vtkAOSDataArrayTemplateInstantiate.cxx` 的改动

### 4.1 文件作用

这个文件是新增的显式模板实例化单元。

内容很小，但作用明确：

- 取消 `VTK_USE_EXTERN_TEMPLATE`
- 包含 `vtkAOSDataArrayTemplate.txx`
- 显式实例化 `vtkAOSDataArrayTemplate<int>`
- 显式实例化 `vtkAOSDataArrayTemplate<long long>`

文件位置：[vtkAOSDataArrayTemplateInstantiate.cxx](../examples/FluentCFFZoneViewer/vtkAOSDataArrayTemplateInstantiate.cxx)

### 4.2 为什么只补这两种

从构建分析看：

- `double` 相关未定义引用主要通过 reader 内部写法调整已经规避
- 剩下最顽固的是 `int` 和 `long long`
- 它们主要来自 `vtkIdTypeArray` / `vtkCellArray` 一类路径的模板实例化

所以这里选择最小补丁面，只补当前链接链路确实需要的类型。

## 5. 改动后的模块职责边界

### 5.1 Reader 模块

`vtkFLUENTCFFReader.cxx` 继续负责：

- Fluent CFF case/data 解析
- 内部拓扑重建
- 输出 VTK 数据对象

新增的只是：

- 更稳的数组写入方式

### 5.2 Example 构建模块

`examples/FluentCFFZoneViewer/CMakeLists.txt` 现在不仅是示例 UI 工程配置，还承担：

- 针对当前工具链把本地 reader 静态化
- 将模板实例化补丁一并编入本地库

这意味着它目前带有一定“平台兼容补丁工程”的性质。

### 5.3 模板实例化模块

`vtkAOSDataArrayTemplateInstantiate.cxx` 是纯构建兼容层：

- 不参与业务逻辑
- 不参与 Fluent 数据解释
- 只服务于链接期

## 6. 对后续维护的建议

如果后续把这个 reader 真正合回独立模块或上游工程，建议优先按下面顺序处理：

1. 先确认目标平台的 VTK 分发是否已经正确导出相关 `vtkAOSDataArrayTemplate<T>` 符号
2. 若上游已修复，可考虑移除本地实例化补丁
3. 若仍需兼容 MSYS2 预编译包，保留当前补丁结构是合理的

当前这套改动的优点是：

- 改动面小
- 不侵入 Fluent 拓扑逻辑
- 问题定位清晰

代价是：

- 示例工程承担了一部分工具链兼容职责
- 标准 `cmake --build` 的行为在当前环境里仍建议继续观察

---

## 7. 新增功能 API 详细说明

本节记录 `vtkFLUENTCFFReader` 相比原始 VTK Reader 新增的 API 功能。

### 7.0 Cell Zone 信息查询 API

**新增方法：**

```cpp
// vtkFLUENTCFFReader.h:188-203
int GetCellZoneType(int zoneId) const;
int GetCellZoneCount() const;
int GetCellZoneIdAtIndex(int index) const;
```

**功能说明：**
- `GetCellZoneType`: 获取指定 Cell Zone 的 zoneType 值（0/1=流体，2=固体），返回 -1 表示未找到
- `GetCellZoneCount`: 获取输出中 Cell Zone 区块数量（等于 CellZones.size()）
- `GetCellZoneIdAtIndex`: 根据区块索引获取对应的 Fluent Cell Zone ID（0 <= index < GetCellZoneCount()）

**新增数据结构：**

```cpp
// vtkFLUENTCFFReader.h:449-450
std::vector<int> CellZonesType;           // 每个 Cell Zone 的类型
std::map<int, int> CellZoneIdToType;       // Zone ID 到类型的映射
```

**实现位置：** `vtkFLUENTCFFReader.cxx:1138-1167`

---

### 7.1 Cell 实体查询 API

**新增方法：**

```cpp
// vtkFLUENTCFFReader.h:205-210
int GetCellType(vtkIdType cellId) const;
int GetCellZoneId(vtkIdType cellId) const;
vtkIdType GetCellNumberOfFaces(vtkIdType cellId) const;
int GetCellFaceId(vtkIdType cellId, vtkIdType localFaceId) const;
vtkIdType GetCellNumberOfNodes(vtkIdType cellId) const;
int GetCellNodeId(vtkIdType cellId, vtkIdType localNodeId) const;
```

**功能说明：**
| 方法 | 功能 | 返回值 |
|------|------|--------|
| `GetCellType` | 获取 Cell 的 VTK 类型 | VTK cell type 或 -1 |
| `GetCellZoneId` | 获取 Cell 所属的 Zone ID | Zone ID 或 -1 |
| `GetCellNumberOfFaces` | 获取 Cell 包含的面数量 | 面数量或 0 |
| `GetCellFaceId` | 获取 Cell 的局部面 ID 对应的全局面 ID | 全局面 ID 或 -1 |
| `GetCellNumberOfNodes` | 获取 Cell 包含的节点数量 | 节点数量或 0 |
| `GetCellNodeId` | 获取 Cell 的局部节点 ID 对应的全局节点 ID | 全局节点 ID 或 -1 |

**实现位置：** `vtkFLUENTCFFReader.cxx:1182-1251`

---

### 7.2 Cell 数据查询 API

**新增方法：**

```cpp
// vtkFLUENTCFFReader.h:221-224
int GetCellArrayComponents(const char* name) const;
double GetCellArrayValue(const char* name, vtkIdType cellId, int component = 0) const;

// 同样适用于 Face 数据
int GetFaceArrayComponents(const char* name) const;
double GetFaceArrayValue(const char* name, vtkIdType faceId, int component = 0) const;
```

**功能说明：**
- `GetCellArrayComponents`: 获取 Cell 数据数组的分量数量
- `GetCellArrayValue`: 直接查询指定 Cell ID 和分量索引处的 Cell 数据值
- 支持直接随机访问，无需遍历整个数据数组

**实现位置：** `GetCellArrayValue` 在 `vtkFLUENTCFFReader.cxx` 中通过 `FindDataChunk` 和直接偏移计算实现

---

### 7.3 Cell 索引缓存 (CellIndicesByZone)

**新增数据结构：**

```cpp
// vtkFLUENTCFFReader.h:482
std::vector<std::vector<vtkIdType>> CellIndicesByZone;
```

**功能说明：**
- 按 Cell Zone 索引的 Cell ID 列表
- 在 `RequestData` 中建立映射：`zoneToLocation` + `cellLocationByIndex`
- 替代逐个 Cell 遍历的 O(n²) 查找，改为直接按 Zone 索引访问

**优化效果：**
- Cell 数据注入时，直接通过 `CellIndicesByZone[location]` 获取该 Zone 内的所有 Cell ID
- UDM 数据拆分时，无需重复遍历所有 Cell

**实现位置：** `vtkFLUENTCFFReader.cxx:341-365`

---

### 7.4 Cell 节点内存池优化

**Cell 结构体扩展字段：**

```cpp
// vtkFLUENTCFFReader.h:252-257
struct Cell
{
  // ... 原有字段 ...
  vtkIdType nodePoolOffset = 0;    // 节点池偏移量
  int nodePoolCount = 0;           // 节点池计数
  vtkIdType nodeOffsetPoolOffset = 0;
  int nodeOffsetPoolCount = 0;
  vtkIdType uniqueNodePoolOffset = 0;
  int uniqueNodePoolCount = 0;
};
```

**功能说明：**
- 针对 Polyhedron 单元，使用内存池模式存储节点索引
- 避免为每个 Cell 单独分配 `std::vector<int> nodes`
- 通过 `nodePoolOffset` + `nodePoolCount` 直接从 `CellNodePool` 数组中读取节点
- 实现位置：`GetCellNodeId()` 中同时支持 pool 和 vector 两种模式

---

### 7.5 Face Zone 区域 PolyData 创建

**新增方法：**

```cpp
// vtkFLUENTCFFReader.h:226-227
vtkSmartPointer<vtkPolyData> CreateFaceZonePolyData(const char* zoneName,
  const char* faceArrayName = nullptr, int component = 0) const;
```

**功能说明：**
- 根据面区域名称创建对应的 `vtkPolyData` 对象
- 可选地附加面数组标量数据（支持多分量数组的分量选择）
- 内部使用 `FaceZoneTopologyCache` 缓存拓扑结构，避免重复构建

**实现位置：** `vtkFLUENTCFFReader.cxx:1446-1510`

---

### 7.6 Face Zone 重叠查询

**新增方法：**

```cpp
// vtkFLUENTCFFReader.h:239
int GetFaceZoneIndicesOverlappingFaceArray(const char* faceArrayName, vtkIntArray* outZoneIndices) const;
```

**功能说明：**
- 查询哪些 Face Zone 的 Fluent 全局面 ID 范围与指定面数组的 HDF5 存储区间有交集
- 返回与该面数组数据相关的面区域索引列表
- 支持 RenameArrays 重命名后的数组名和多相流后缀（如 `SV_P-phase_1`）

**返回值：**
- 正数：重叠的面区域数量
- 0：无重叠
- -1：参数无效或数组未知

**实现位置：** `vtkFLUENTCFFReader.cxx:1339-1388`

---

### 7.7 Face Zone 拓扑缓存

**新增数据结构：**

```cpp
// vtkFLUENTCFFReader.h:484-489
struct FaceZoneTopologyCache
{
  bool Built = false;
  vtkSmartPointer<vtkCellArray> Polys;
  std::vector<vtkIdType> FaceIds;
};
```

**功能说明：**
- 缓存每个面区域的多边形拓扑结构
- 延迟构建（Lazy Build）模式，首次访问时构建
- 避免每次调用 `CreateFaceZonePolyData` 时重复遍历Faces

**成员变量：** `mutable std::vector<FaceZoneTopologyCache> FaceZoneTopologyCaches;`

**实现位置：** `vtkFLUENTCFFReader.cxx:1391-1443` (`EnsureFaceZoneTopologyCache`)

---

### 7.8 DataChunk 扩展

**扩展字段：**

```cpp
// vtkFLUENTCFFReader.h:460
std::vector<std::pair<std::uint64_t, std::uint64_t>> FaceSectionFluentIdRanges1Based;
```

**功能说明：**
- 记录 Face 类型数据在 HDF5 中存储的 Fluent 全局面 ID 区间（1-based， inclusive）
- 用于 `GetFaceZoneIndicesOverlappingFaceArray` 的Overlap检测
- 在 `ReadDataForType` 中填充

---

### 7.9 Face Zone 详细查询 API

**新增方法：**

```cpp
// 基本数量查询
int GetNumberOfFaceArrays() const;
int GetNumberOfFaceZones() const;
vtkIdType GetNumberOfNodesRead() const;
vtkIdType GetNumberOfFacesRead() const;

// Face Zone 名称/索引查询
const char* GetFaceZoneName(int index) const;
int GetFaceZoneIndexByName(const char* name) const;

// Face Zone ID 查询
int GetFaceZoneIdByName(const char* name) const;
const char* GetFaceZoneNameById(int zoneId) const;
int GetFaceZoneType(int zoneId) const;

// Face Zone ID 范围查询
vtkIdType GetFaceZoneFirstFaceIdByName(const char* name) const;
vtkIdType GetFaceZoneLastFaceIdByName(const char* name) const;
vtkIdType GetFaceZoneSizeByName(const char* name) const;
int GetFaceIdByZoneName(const char* name, vtkIdType localFaceIndex) const;

// Face 实体查询 API
int GetFaceType(vtkIdType faceId) const;
int GetFaceZoneId(vtkIdType faceId) const;
vtkIdType GetFaceNumberOfNodes(vtkIdType faceId) const;
int GetFaceNodeId(vtkIdType faceId, vtkIdType localNodeId) const;
int GetFaceC0(vtkIdType faceId) const;
int GetFaceC1(vtkIdType faceId) const;

// 节点坐标查询
bool GetNodeCoordinates(vtkIdType nodeId, double coords[3]) const;
```

**功能说明：**
- Face Zone 名称/ID/索引的相互转换
- Face Zone 的 ID 范围查询（首尾 face ID）
- Face Zone 内的局部 face 索引转全局 face ID
- Face 实体信息查询（类型、所属 Zone、节点、邻接 Cell）
- 节点坐标直接查询

**实现位置：** `vtkFLUENTCFFReader.cxx:1039-1310`

---

### 7.13 FaceZoneInfo 数据结构

**新增结构体：**

```cpp
// vtkFLUENTCFFReader.h:279-285
struct FaceZoneInfo
{
  std::string name;
  int id = -1;
  vtkIdType firstFaceId = -1;
  vtkIdType lastFaceId = -1;
};
```

**功能说明：**
- 存储每个 Face Zone 的名称、ID、以及首尾 Face ID 范围
- `firstFaceId` 和 `lastFaceId` 用于面区域的范围查询

**成员变量：** `std::vector<FaceZoneInfo> FaceZones;`

---

### 7.14 Face 结构体扩展

**Face 结构体：**

```cpp
// vtkFLUENTCFFReader.h:262-275
struct Face
{
  int type;
  unsigned int zone;
  vtkIdType nodeOffset = 0;    // 节点池偏移量（类似 Cell 的节点池优化）
  int nodeCount = 0;           // 节点数量
  int c0;                      // 邻接的 Cell 0
  int c1;                      // 邻接的 Cell 1
  int periodicShadow;          // 周期性阴影面
  int parent;
  int child;
  int interfaceFaceParent;
  int interfaceFaceChild;
  int ncgParent;
  int ncgChild;
};
```

**功能说明：**
- 类似于 Cell 的节点池优化，`nodeOffset` + `nodeCount` 直接从 `FaceNodePool` 数组读取节点
- `c0`, `c1` 存储邻接的 Cell ID（用于面心数据查询）
- 支持周期性、父子关系、界面面、非共形网格等复杂拓扑

---

### 7.15 数据选择器管理

**新增方法（数据数组选择）：**

```cpp
// Cell 数据选择
int GetCellArrayStatus(const char* name);
void SetCellArrayStatus(const char* name, int status);
void EnableAllCellArrays();
void DisableAllCellArrays();

// Face 数据选择
int GetFaceArrayStatus(const char* name);
void SetFaceArrayStatus(const char* name, int status);
void EnableAllFaceArrays();
void DisableAllFaceArrays();
```

**功能说明：**
- 提供细粒度的 Cell/Face 数据数组加载控制
- 支持批量启用/禁用数组
- 内部使用 `vtkDataArraySelection` 管理

**成员变量：**
```cpp
vtkNew<vtkDataArraySelection> CellDataArraySelection;
vtkNew<vtkDataArraySelection> FaceDataArraySelection;
```

---

### 7.16 扩展的字段名映射表

**新增映射条目（在 `vtkFLUENTCFFInternal.h` 中）：**

`FieldsNamesMap` 包含了超过80个 Fluent 变量名到可读名称的映射，涵盖：

- 基本流体变量：密度、压力、速度、温度、焓
- 湍流模型：k-epsilon, k-omega, Spalart-Allmaras 等
- 多相流：VOF、相分数、体积分数
- 离散相模型（DPM）：粒子温度、速度、直径、质量等
- 辐射模型：散射系数、吸收系数
- 电化学模型：电导率、过电位、渗透压
- 用户自定义标量（UDS）和动源项

**相关函数：**
- `GetMatchingFieldName()` - 获取映射后的字段名
- `RemoveTrailingIndex()` - 移除尾随数字
- `RemoveSuffixIfPresent()` - 移除特定后缀

---

## 8. 性能优化相关

### 8.1 批量写入优化

为解决 MSYS2/VTK 组合下的链接问题，同时提升了性能：

| 操作 | 原始方式 | 优化后方式 |
|------|----------|------------|
| Polyhedron face 写入 | InsertNextValue | vtkIdTypeArray 直接指针写入 |
| Cell data 数组 | InsertNextTuple | WritePointer 批量写入 |
| UDM 标量拆分 | InsertNextValue | 直接指针操作 |
| Face scalar 输出 | InsertNextValue | WritePointer |
| Node 坐标 | InsertPoint | 底层 vtkDoubleArray 直接写入 |

### 8.2 Face Zone 拓扑缓存

- 首次创建 PolyData 时构建缓存
- 后续相同区域的查询直接返回缓存结果
- 缓存仅在 `ResetMeshState` 时清除

---

## 8. Cell 与 Face 重建实现说明

本节说明 `vtkFLUENTCFFReader` 中 Cell（单元）和 Face（面）拓扑重建的实现方式，以及与原始 VTK `vtkFLUENTReader` 的主要差异。

### 8.1 Cell 重建流程

#### 8.1.1 总体流程

```
1. GetCells() / GetCellsGlobal()
   └─> 读取 HDF5 中 cells/nodes 路径下的拓扑数据
       - 节点坐标 (coords)
       - 节点连接 (nodes)
       - 面关联 (c0, c1)

2. GetNumberOfCellZones()
   └─> 遍历 Cells，收集唯一的 zone ID 列表到 CellZones vector
       - 同时建立 Cell → Zone 映射

3. RequestData() 中的 PopulateCellNodes()
   └─> 根据 Cell 类型调用对应重建函数：
       - PopulateTetCell (type=2)
       - PopulateQuadCell (type=3)
       - PopulateHexahedronCell (type=4)
       - PopulatePyramidCell (type=5)
       - PopulateWedgeCell (type=6)
       - PopulatePolyhedronCell (type=7)
```

#### 8.1.2 各类 Cell 重建方式

| Cell 类型 | type 值 | 重建策略 |
|-----------|---------|----------|
| Tetrahedron | 2 | 直接从 faces 提取 4 个节点，构造 VTK_TETRA |
| Quad | 3 | 从 faces 提取 4 个节点，构造 VTK_QUAD + VTK_POLYGON |
| Hexahedron | 4 | 从 6 个 faces 依次提取节点，构造 VTK_HEXAHEDRON |
| Pyramid | 5 | 5 个面（4 三角 + 1 四边），构造 VTK_PYRAMID |
| Wedge | 6 | 5 个面（2 三角 + 3 四边），构造 VTK_WEDGE |
| Polyhedron | 7 | 从 faces 提取节点并构造变面数单元，使用 Connect 各面的节点 |

#### 8.1.3 Polyhedron 重建（重点优化对象）

Polyhedron（多面体）是最复杂的 Cell 类型，其重建过程：

1. **从 Faces 收集节点**：遍历 Cell 的所有 face，从每个 face 的节点列表中收集节点
2. **节点去重**：由于 faces 之间共享节点，需要去重得到 Cell 的唯一节点列表
3. **构造 VTK_POLYHEDRON**：使用 `vtkCellArray::InsertNextCell` 写入

**当前实现 vs 原始 VTK 差异：**

| 方面 | 原始 VTK vtkFLUENTReader | 当前实现 |
|------|--------------------------|----------|
| 节点存储 | 每个 Cell 独立 vector | CellNodePool 内存池 + offset/count |
| 节点去重 | 每次重建时重新去重 | 预计算 UniqueNodePool（已优化） |
| 写入方式 | `InsertNextCell` 逐个写入 | 批量 `SetData` + 指针操作 |
| face data | 按 face 遍历获取节点 | 预建 FaceZoneTopologyCache |

### 8.2 Face 重建流程

#### 8.2.1 总体流程

```
1. GetFaces() / GetFacesGlobal()
   └─> 读取 HDF5 中 faces/nodes 路径下的拓扑数据
       - 每面节点数 (nnodes)
       - 节点ID列表 (nodes)
       - c0, c1 (单元关联)

2. GetNumberOfFaceZones()
   └─> 遍历 Faces，收集唯一的 zone ID 列表到 FaceZones vector

3. 创建 Face Zone PolyData
   └─> CreateFaceZonePolyData(zoneName)
       - 先检查 FaceZoneTopologyCache
       - 缺失则遍历 Faces 收集属于该 zone 的面
       - 构造 vtkPolyData (points + polys)
```

#### 8.2.2 Face Zone 拓扑缓存

**新增数据结构：**

```cpp
struct FaceZoneTopologyCache
{
  bool Built = false;
  vtkSmartPointer<vtkCellArray> Polys;
  std::vector<vtkIdType> FaceIds;
};
std::mutable std::vector<FaceZoneTopologyCache> FaceZoneTopologyCaches;
```

**作用：**
- 首次调用 `CreateFaceZonePolyData` 时构建并缓存
- 后续相同 zone 的查询直接返回缓存
- 仅在 `ResetMeshState` 时清除

### 8.3 与原始 VTK 模块的关键差异总结

| 模块 | 原始 VTK vtkFLUENTReader | vtkFLUENTCFFReader（本项目） |
|------|--------------------------|------------------------------|
| HDF5 读取 | 仅支持传统格式 (.cas/.msh) | 支持 CFF HDF5 格式 (.cas.h5/.dat.h5) |
| Cell 节点存储 | 独立 vector | CellNodePool 内存池 + offset |
| Polyhedron 去重 | 运行时每次去重 | UniqueNodePool 预计算 |
| Face Zone 缓存 | 无缓存 | FaceZoneTopologyCache |
| API 扩展 | 基础读取 | 增加 zone 查询、cell 随机访问等 |
| 性能优化 | 标准 VTK 流程 | 批量写入、预分配、缓存优化 |

### 8.4 面面积计算

`BuildGraphData()` 中新增 **FaceAreas** 和 **CellFaceAreas** 数组。

**计算方式：**

对所有面（含内部和边界），使用多边形精确面积公式：

```
area = 0.5 * |Σ(vi × v_{i+1})|  (下标循环)
```

- **三角形**：等价于标准叉积 `|(v1-v0)×(v2-v0)|/2`
- **四边形及以上**：Newell 方法，对任意平面多边形精确；对翘曲面给出最佳拟合投影面积
- 面积以 `float` 精度存储

**三个数组：**

| 数组 | 长度 | 含义 |
|------|------|------|
| `FaceAreasAll` | `[F]`（= `Faces.size()`） | 所有面（内部 + 边界）的面积，仅内部使用 |
| `FaceAreas` | `[M]`（= 边界面数） | 仅边界面的面积，与 `FaceCentroids`/`FaceNormals` 顺序对齐 |
| `CellFaceAreas` | `[E]`（= `EdgeIndexSrc.size()`） | 每条有向边对应面的面积，与 `edge_index` 列严格对齐；同一内部面的正反向边取相同面积值 |

**公开 API：**
- `GetFaceAreas()` → `FaceAreas` 裸指针（仅边界面，与 GetFaceCentroids/GetFaceNormals 一致）
- `GetFaceAreaCount()` → `FaceAreas.size()`（= `GetBoundaryFaceCount()`）
- `GetCellFaceAreas()` → `CellFaceAreas` 裸指针
- `GetCellFaceAreaCount()` → `CellFaceAreas.size()`

**GNN 下游用途：** `CellFaceAreas` 作为 `edge_weight` 传入 GraphSAGE 的 `SAGEConv(h, edge_index, edge_weight=edge_weight)` 中，替代默认的均值聚合，使大面积面的信息传递权重大于小面积面。

---

## 9. 文件对照表

| 文件路径 | 说明 | 新增/修改 |
|----------|------|-----------|
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h` | Reader 头文件 | 修改 - 新增 API |
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx` | Reader 实现 | 修改 - 新增功能实现 |
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFInternal.h` | 内部工具头文件 | 修改 - 扩展字段映射表 |
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFInternal.cxx` | 内部工具实现 | 既有实现 |
| `vtk/IO/FLUENTCFF/vtk.module` | CMake 模块定义 | 既有文件 |
| `vtk/IO/FLUENTCFF/vtkIOFLUENTCFFModule.h` | 导出宏 | 既有文件 |
| `examples/FluentCFFZoneViewer/vtkAOSDataArrayTemplateInstantiate.cxx` | 模板实例化补丁 | 新增 - 构建兼容 |
