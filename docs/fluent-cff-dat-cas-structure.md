# Fluent CFF `.cas/.dat` 数据结构与实际 HDF5 路径总结

## 1. Reader 的主流程

`vtkFLUENTCFFReader` 的读取分两段：

1. `RequestInformation()`
   - 打开 `.cas.h5`
   - 自动尝试打开同名 `.dat.h5`
   - 预读 `/results/1/phase-N/{cells|faces}` 下有哪些字段
2. `RequestData()`
   - 读取 `/meshes/1/...` 拓扑
   - 重建 `Cells` / `Faces` / `vtkPoints`
   - 读取 `/results/1/phase-N/{cells|faces}/{field}/{section}`
   - 把数值注入到全局 cell/face 索引，再输出 VTK 对象

关键代码：

- `RequestInformation()`：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L417)
- `RequestData()`：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L138)
- `ParseCaseFile()`：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1012)

## 2. 内存中的核心结构

定义位置：[vtkFLUENTCFFReader.h](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h#L195)

### 2.1 `Cell`

- `type`: 单元类型
- `zone`: 所属 cell zone id
- `faces`: 该 cell 挂接的 face id 列表
- `nodes`: 重建后的 VTK 节点顺序
- `nodesOffset`: polyhedron 用的 face 偏移
- `parent` / `child` / `childId`: tree/AMR 关系

### 2.2 `Face`

- `type`: 该 face 的节点数
- `zone`: 所属 face zone id
- `nodes`: 该 face 的节点 id 列表
- `c0` / `c1`: 该 face 两侧的 cell id
- `periodicShadow` / `parent` / `child` / `interfaceFaceParent` / `interfaceFaceChild` / `ncgParent` / `ncgChild`: 特殊拓扑标记

### 2.3 `FaceZoneInfo`

- `name`
- `id`
- `firstFaceId`
- `lastFaceId`

这个结构不是“附加说明”，而是 face 分类的核心索引。

### 2.4 `DataChunk`

定义位置：[vtkFLUENTCFFReader.h](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h#L403)

- `variableName`
- `dim`
- `dataVector`

Reader 按实体类型分两组保存：

- `CellDataChunks`
- `FaceDataChunks`

## 3. `.cas.h5` 如何加载拓扑

### 3.1 全局容量路径

先从 `/meshes/1` 读取全局大小：

- `nodeCount` -> `GetNodesGlobal()`
- `cellCount` -> `GetCellsGlobal()`
- `faceCount` -> `GetFacesGlobal()`

对应代码：

- [vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1075)
- [vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1257)
- [vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1467)

这一步只分配：

- `vtkPoints`
- `Cells`
- `Faces`

### 3.2 Nodes 的实际路径

节点拓扑和坐标来自两部分：

- `/meshes/1/nodes/zoneTopology`
- `/meshes/1/nodes/coords/<zoneId>`

`GetNodes()` 先从 `/meshes/1/nodes/zoneTopology` 读取：

- `nZones`
- `minId`
- `maxId`
- `id`
- `name`
- `dimension`

然后再去 `/meshes/1/nodes/coords/<zoneId>` 读取真正坐标，按全局 node id 写入 `vtkPoints`：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1106)

要点：

- 文件里是 1-based id
- 内部转换成 0-based
- 2D 数据补 `z = 0.0`

### 3.3 Cells 的实际路径

cell 基础信息来自：

- `/meshes/1/cells/zoneTopology`

`GetCells()` 读取：

- `minId`
- `maxId`
- `id`
- `dimension`
- `cellType`
- `childZoneId`

对应代码：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1285)

填充结果：

- `Cells[i].zone`
- `Cells[i].type`

如果某个 zone 是 mixed type，还会继续去：

- `/meshes/1/cells/ctype/<section>/cell-types`

为区间里的每个 cell 单独赋类型。

### 3.4 Faces 的实际路径

face 拓扑主要由三类路径组成：

- `/meshes/1/faces/zoneTopology`
- `/meshes/1/faces/nodes/<section>`
- `/meshes/1/faces/c0` 和 `/meshes/1/faces/c1`

对应代码：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1495)

#### 3.4.1 `/meshes/1/faces/zoneTopology/name` 如何对 faces 分类

`GetFaces()` 会读取：

- `/meshes/1/faces/zoneTopology/name`

并把它解析成 `zoneNames`。这个数据集不是摆设，它和同层的：

- `minId`
- `maxId`
- `id`

一起决定 face zone 的划分。

Reader 为每个 zone 建立：

```text
FaceZoneInfo {
  name = zoneNames[i]
  id = Id[i]
  firstFaceId = minId[i] - 1
  lastFaceId = maxId[i] - 1
}
```

对应代码：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1609)

同时该区间内的每个 face 都会写入：

```text
Faces[faceId].zone = zoneId
```

所以 face 分类有两套同步索引：

- `FaceZoneInfo`：从名字出发找到 face 区间
- `Face.zone`：从 face 出发找到所属 zone id

#### 3.4.2 `/meshes/1/faces/nodes/<section>` 如何形成 `Face.nodes`

在每个 `/meshes/1/faces/nodes/<section>` 下：

- `nnodes` 记录每个 face 的节点数
- `nodes` 是扁平节点数组

Reader 逐 face 切片，填到：

- `Faces[i - 1].nodes`
- `Faces[i - 1].type`

对应代码：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1655)

#### 3.4.3 `/meshes/1/faces/c0` 和 `/meshes/1/faces/c1` 如何形成 `Cell.faces`

Reader 读取：

- `/meshes/1/faces/c0/<section>`
- `/meshes/1/faces/c1/<section>`

并填：

- `Faces[faceId].c0`
- `Faces[faceId].c1`

只要 `c0` 或 `c1` 有效，就会把该 `faceId` 挂到对应：

- `Cells[cellId].faces`

对应代码：

- `c0`：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1727)
- `c1`：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1780)

### 3.5 特殊拓扑

基本拓扑后还会尝试读取：

- `/meshes/1/cells/tree`
- `/meshes/1/faces/tree`
- 其他特殊拓扑信息

入口在 `ParseCaseFile()`：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L1012)

## 4. 拓扑如何变成最终 VTK 单元

### 4.1 `CleanCells()`

会删掉不应参与几何重建的 face：

- child
- ncg child
- interface child

位置：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L2189)

### 4.2 `PopulateCellNodes()`

会根据 `Cell.type` 调 `Populate*Cell()`，把：

- `Cell.faces`
- `Face.nodes`
- `Face.c0/c1`

重建成 VTK 所需的 `Cell.nodes`。

入口位置：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L2263)

polyhedron 例外：

- `Cell.nodes` 是扁平 face-node 列表
- `Cell.nodesOffset` 是每个 face 的偏移

位置：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L2810)

## 5. `.dat.h5` 的真实路径结构

当前代码里，不建议再用抽象的 `label/to/data` 作为主描述，更准确的是直接写实际路径：

- cell 字段根路径：`/results/1/phase-N/cells`
- face 字段根路径：`/results/1/phase-N/faces`
- 字段列表：`/results/1/phase-N/{cells|faces}/fields`
- 单字段 group：`/results/1/phase-N/{cells|faces}/{field}`
- 单 section 数据：`/results/1/phase-N/{cells|faces}/{field}/{section}`

如果一定要对应原来的抽象：

- `label` = `{field}`
- `to` = `cells` 或 `faces`
- `data` = `{section}` dataset 加上其 `minId/maxId`

## 6. `/results/1/phase-1/faces/{field}/{1|2}` 如何注入到拓扑

这是 face 数据和拓扑结合的关键。

对应实现：`ReadDataForType("faces", ...)`  
位置：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L2989)

### 6.1 从 `fields` 找到 `{field}`

Reader 先读：

```text
/results/1/phase-1/faces/fields
```

把字段名列表拆开，然后依次进入：

```text
/results/1/phase-1/faces/{field}
```

### 6.2 从 `{field}` 读取 `nSections`

Reader 读取该字段 group 的：

```text
nSections
```

如果是 2，就说明存在：

```text
/results/1/phase-1/faces/{field}/1
/results/1/phase-1/faces/{field}/2
```

### 6.3 每个 section 先读 `minId/maxId`

对 `/results/1/phase-1/faces/{field}/1`，先读：

- `minId`
- `maxId`

对 `/results/1/phase-1/faces/{field}/2` 也一样。

这两个属性定义的是“这个 section 覆盖的全局 face id 区间”。

### 6.4 section 数据写入 `FaceDataChunks`

Reader 会创建一个 `DataChunk`：

- `chunk.variableName = fieldName`
- `chunk.dim = numberOfComponents`
- `chunk.dataVector.size() = Faces.size() * dim`

初始化位置：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L3150)

然后对区间内每个全局 id `j` 回填：

```text
tupleIndex = j - 1
chunk.dataVector[tupleIndex * dim + component] = sectionValue
```

对应代码：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L3157)

也就是说：

- `/results/1/phase-1/faces/{field}/1`
  负责把某段 `faceId` 的值写进 `FaceDataChunks[field]`
- `/results/1/phase-1/faces/{field}/2`
  再把另一段 `faceId` 的值写进同一个 `FaceDataChunks[field]`

它们不是各自形成独立拓扑，而是共同填充同一张“按全局 face id 编址”的数组。

### 6.5 为什么说这是“注入到拓扑”

因为 case 文件那边已经先建立了：

- `Faces[faceId]`
- `Face.zone`
- `Face.nodes`
- `Face.c0/c1`
- `FaceZoneInfo(name, firstFaceId, lastFaceId)`

而结果文件只负责按同一个全局 `faceId` 填数值。

所以结合点就是：

```text
全局 face id
```

不是 zone 名，也不是 section 序号。

## 7. face zone 如何消费这些 face 数据

当调用：

```cpp
CreateFaceZonePolyData(zoneName, faceArrayName, component)
```

Reader 会：

1. 用 `zoneName` 找 `FaceZoneInfo`
2. 得到 `[firstFaceId, lastFaceId]`
3. 遍历这段全局 face id
4. 用 `Faces[faceId].nodes` 生成 polygon
5. 用 `GetFaceArrayValue(faceArrayName, faceId, component)` 去 `FaceDataChunks` 取值

位置：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L917)

而 `GetFaceArrayValue()` 的本质只是：

```text
offset = faceId * dim + component
return dataVector[offset]
```

位置：[vtkFLUENTCFFReader.cxx](/F:/Users/20968/projects/ai/gnn/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.cxx#L903)

## 8. 一个具体示例

假设：

- `/meshes/1/faces/zoneTopology/name` 里某一项是 `wall`
- 对应 `minId = 101`
- 对应 `maxId = 160`

那么内存中会得到：

```text
FaceZoneInfo {
  name = "wall"
  firstFaceId = 100
  lastFaceId = 159
}
```

再假设：

- `/results/1/phase-1/faces/SV_P/1` 覆盖 `minId=1, maxId=120`
- `/results/1/phase-1/faces/SV_P/2` 覆盖 `minId=121, maxId=300`

那 Reader 会：

1. 用 section `1` 填 `faceId 0..119` 的 `SV_P`
2. 用 section `2` 填 `faceId 120..299` 的 `SV_P`

于是 `wall` zone 内：

- `faceId 100..119` 的值来自 section `1`
- `faceId 120..159` 的值来自 section `2`

但对外访问已经统一成：

```cpp
GetFaceArrayValue("SV_P", faceId, 0)
```

或：

```cpp
CreateFaceZonePolyData("wall", "SV_P", 0)
```

## 9. 最重要的结论

当前 reader 的真实数据流应该这样理解：

- `/meshes/1/faces/zoneTopology/name + minId/maxId + id`
  定义 face zone 分类
- `/meshes/1/faces/nodes/*`
  定义每个 face 的几何节点
- `/meshes/1/faces/c0` 和 `/meshes/1/faces/c1`
  定义 face 与 cell 的邻接关系
- `/results/1/phase-N/faces/{field}/{section}`
  按 `minId/maxId` 把数值写进全局 face id 数组

因此 `/results/1/phase-1/faces/{field}/{1|2}` 注入到拓扑的路径不是：

- 通过 zone name 注入

而是：

```text
section.minId/maxId
-> 全局 face id 区间
-> FaceDataChunks[field].dataVector
-> GetFaceArrayValue(faceId)
-> FaceZoneInfo 选出的 face 区间
-> CreateFaceZonePolyData(zoneName, field)
```

这就是 face 结果从 HDF5 section 进入拓扑对象的实际链路。
