# 未实现管线备忘（Backlog）

本文档记录在实现 **边界编码三模块 + GraphSAINT（路径 B）** 主计划之外、讨论过但**尚未编码**或可拆期迭代的管线。执行主计划过程中若范围或优先级变化，在此**增补/划掉**，并视需要**同步修订**仓库根目录 [`总观.md`](../总观.md)（架构叙述以可落地为准，避免设计与代码长期脱节）。

---

## 与当前首轮计划的关系

- **首轮默认**：在 Fluent 导出的 **观测网格** 上构造 `Data(x, edge_index, edge_weight, y)`，用 **GraphSAINT / NeighborLoader** 做子图训练；**边界三模块**输出固定维 **`z`** 条件内部 GNN。
- **总观 4.4 新增要求**：内部拓扑不再只是无权 cell 邻接图，而是以共享面面积为权重的图 `G_cell=(V,E,w)`；神经网络输入必须是包含 `edge_weight` 的有权图；超大图在训练/推理前需要支持 \(\textbf{Benczúr-Karger / weighted cut sparsification}\)，并保持稀疏化后的 `edge_index` 与重加权 `edge_weight` 对齐。
- 下列条目 **不拦截** 首轮交付；完成后按优先级从中挑选迭代。

---

## 总观 4.4 变更带来的框架适配点

现有框架已经开始暴露 `face_areas` / `cell_face_areas`，并在 Dataset / PyG `Data` 中传递 `edge_weight`。要完整适配新需求，还需要补齐以下框架层变动：

1. **拓扑语义固定**：将内部拓扑正式定义为面积加权 cell 图。`edge_index` 的每条有向边必须有同序 `edge_weight`，正反向边应共享同一内部面面积。
2. **稀疏化阶段独立成管线**：新增 weighted cut sparsification 前处理阶段，输入完整面积加权图，输出稀疏 `edge_index`、重加权 `edge_weight`、采样概率/随机种子/`\epsilon` 等元数据。
3. **缓存与失效规则升级**：`FluentCFFGNNDataset` 的 manifest / shard key 需要纳入图前处理配置，例如 `sparsifier=none|bk_cut`、`\epsilon`、目标边数或采样倍率、随机种子。配置变化时必须重建拓扑分片。
4. **神经网络输入契约升级**：内部流场网络的输入必须是有权图 `(x, edge_index, edge_weight)`；模型层不能只把 `edge_weight` 挂在 `Data` 上，也不能在 forward 中把它当作可忽略的可选项。
5. **训练层真正消费权重**：当前内部 GNN 需要确认所选 PyG 卷积是否支持加权消息传递；若不支持，必须替换为支持 `edge_weight` 的卷积或实现自定义 weighted message passing。
6. **采样策略关系重审**：Benczúr-Karger 是图级稀疏化，GraphSAINT / NeighborLoader 是训练时子图采样。二者叠加后需要验证 cut 保真、连通性、边界层/喉部/分叉区域是否被过度采样或过度删边。
7. **评估指标补充**：除训练 loss 外，需要记录稀疏率、cut capacity 近似误差、连通分量变化、关键 zone 的边保留率，以及同一训练预算下的精度/吞吐对比。

---

## Backlog 条目

### 1. 生成式管线（先点云 / 拓扑，再场）

- **来源**：[`总观.md`](../总观.md) 中「体积折叠网络」「神经连边器」等阶段。
- **状态**：未实现。
- **说明**：与「在 **给定** `internal_coords` + `edge_index` 上做 SAINT」不同；需单独损失（Chamfer、边 BCE 等）与训练循环。落地后可在 `总观.md` 中标注「已实现模块」与依赖版本。

### 2. 跨 cas 并行 batch（PyG `Batch`）

- **状态**：首轮刻意不做（调试优先）。
- **说明**：多不相交图拼一张 `Batch`，提高吞吐；需与可变规模 collate、`batch` 向量对齐。

### 3. 节点与边的联合嵌入（同为 ℝ^d）

- **状态**：计划中讨论过，首轮可只做节点场回归。
- **说明**：边嵌入 `e_{uv}=φ([h_u,h_v,...])`、线图等；「同一维度」指 **嵌入维 `d`**，非 \|V\|/\|E\| 相同。

### 4. 跨 cas **结构对齐**（固定 \|V\|/\|E\| 或模板图）

- **状态**：未实现。
- **说明**：需重采样、粗化、潜空间格点等；与 SAINT 在观测图上训练是不同目标。

### 5. 超大 `edge_index` I/O 与内存

- **状态**：Dataset 已四分片缓存；训练侧未做 mmap/Zarr 分块，也未做 weighted cut sparsification。
- **说明**：若全图装入 GPU 仍困难，优先评估总观 4.4 的面积加权 cut sparsification；如仍不足，再评估 mmap/Zarr 分块或边子集持久化。

### 6. 完整 MeshGraphNets 级处理器

- **状态**：未实现。
- **说明**：首轮以浅层 `MessagePassing` + SAINT 为主。

### 7. 训练依赖与环境

- **状态**：`torch-geometric` 等未写入仓库顶层依赖锁定文件（待 `requirements-train.txt` 或等价）。
- **说明**：与 MSVC + vcpkg + `.venv` 约定见 [`AGENT.md`](../AGENT.md)、[`FluentCFFGNNPy-build-troubleshooting.md`](FluentCFFGNNPy-build-troubleshooting.md)。

### 8. 内部拓扑前处理：面积加权图 + weighted cut sparsification

- **来源**：[`总观.md`](../总观.md) 4.4「内部拓扑前处理：面积加权图与 Cut Sparsification」。
- **状态**：部分实现。面积权重链路已开始落地（`cell_face_areas` → `edge_weight`），但 Benczúr-Karger / weighted cut sparsification 尚未实现。
- **需要变动现有框架**：
  - `vtkFLUENTCFFReader` / `FluentCFFGNNExporter`：继续保证 `cell_face_areas` 与 `edge_index` 严格同序，并明确有向边的权重复制规则。
  - `python/fluentcff_gnn_dataset.py`：拓扑 cache 需要区分完整图与稀疏图，并把稀疏化配置写入 `manifest.json` 与 shard meta。
  - `python/fluentcff_gnn_features.py`：`build_pyg_data()` 需要可选择输出完整图或稀疏图，但两者都必须输出 `edge_weight`；同时保留稀疏化元数据供训练日志记录。
  - `python/fluentcff_internal_gnn.py`：内部 GNN forward 契约应要求输入有权图，消息传递实现必须真正使用 `edge_weight`；若当前卷积层不支持权重，需要替换或自定义。
  - `test/`：新增稀疏化一致性测试，至少覆盖边权重重标定、cut 近似抽样检查、`edge_index`/`edge_weight` 对齐、cache 配置变更触发重建，以及模型 forward 缺少 `edge_weight` 时应失败或显式降级。
- **验收标准**：给定同一完整面积加权图，稀疏化后边数显著下降；`edge_weight` 为 $w/p$ 重加权；主要连通结构保持；训练脚本和内部 GNN 均以有权图作为输入并实际消费边权。

---

## 维护约定

1. 从本 backlog **立项实现**时：在 PR/提交说明中引用条目编号；实现后在此 **改为「已实现」并链到模块路径**。
2. 若架构级假设变更（例如默认改为生成式管线），**更新 `总观.md`** 对应章节，并在本文件顶部 **记一行变更日期与摘要**。
3. 与 [`HISTORY.md`](../HISTORY.md) 的关系：代码层面改动仍按 AGENT 约定追加 HISTORY；本文件偏 **产品/架构待办**，二者可交叉引用。

---

## 变更记录

| 日期 | 摘要 |
|------|------|
| 2026-05-11 | 明确神经网络输入必须是有权图 `(edge_index, edge_weight)`，并将该契约写入框架适配与验收标准 |
| 2026-05-11 | 根据 `总观.md` 4.4 增补面积加权内部拓扑与 weighted cut sparsification 需求，并列出现有框架适配点 |
| 2026-04-20 | 初稿：从边界编码 + GraphSAINT 计划讨论中抽出未实现管线 |
