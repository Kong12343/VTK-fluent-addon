# Fluent CFF 文档索引（docs/）

本目录聚焦 Fluent CFF（`.cas.h5` / `.dat.h5`）在本仓库 `vtkFLUENTCFFReader` 中的**真实读取路径**、**拓扑重建链路**与**数据注入方式**，以及为了在当前工具链可构建/可验证而做过的模块改动摘要。

## 推荐阅读顺序

1. `fluent-cff-dat-cas-structure.md`
   - 从 Reader 的主流程出发，串起：case 拓扑 → VTK 单元重建 → dat 字段注入 → face zone 消费数据。
2. `cff-v21-cas-dat-hdf5-field-tree.md`
   - 更“贴文件”的 HDF5 树与字段清单，包含 `data/v21` 的实测样例树（便于对照你手头的 `.cas.h5/.dat.h5`）。
3. `fluent-cff-modified-modules.md`
   - 记录为了构建/链接兼容与后续性能验证流程而做过的改动模块归档（“改了哪些、为什么、影响是什么”）。

## 目录约定

- **主文档目录**：`docs/`
- **兼容入口目录**：`doc/`（单数），仅用于把误写/误记的入口指到 `docs/`。入口文件见 `../doc/README.md`。

