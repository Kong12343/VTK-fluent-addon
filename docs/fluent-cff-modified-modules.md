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
