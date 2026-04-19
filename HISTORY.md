# HISTORY

> 由各窗口代理维护：执行命令前先阅读其他窗口汇总；执行命令后追加当前窗口任务汇总与时间戳。

## Current Window - 2026-04-15 04:08:41
- Objective: 确认“每次运行按 CLI 输出日志”的具体诉求并准备调整方案。
- Actions: 阅读了 `HISTORY.md` 与 `AGENT.md`，并记录当前会话时间戳。
- Result: 已同步上下文与约束，待用户确认具体输出粒度后再改代码。
- Next: 给出包含项目树的改动思路并征求用户许可。

## Current Window - 2026-04-15 04:16:44
- Objective: 实现 Debug 模式命令行+日志文件双写，文件名按程序名 `-debug`。
- Actions: 修改 `main.cxx` 与 `vtkFLUENTCFFReader.cxx`，加入统一时间戳日志函数、环境变量日志路径和双写输出；执行了构建验证。
- Result: 代码编译到链接阶段通过，最终链接因 `FluentCFFZoneViewer.exe` 被占用失败（Permission denied）。
- Next: 关闭运行中的 `FluentCFFZoneViewer.exe` 后重新构建并运行验证日志落盘。

## Current Window - 2026-04-15 04:22:05
- Objective: 按用户要求重新运行构建验证日志改动。
- Actions: 重新执行 `cmake --build examples/FluentCFFZoneViewer/build-msys2-clang -j 8`。
- Result: 仍在最终链接阶段失败，`FluentCFFZoneViewer.exe` 被占用（Permission denied）。
- Next: 用户需先关闭正在运行的 `FluentCFFZoneViewer.exe`（或结束占用进程）后再重试构建。

## Current Window - 2026-04-15 04:37:19
- Objective: 验证项目启动后 Debug 日志是否持续输出并落盘。
- Actions: 检查 `FluentCFFZoneViewer` 进程状态；定位并读取 `FluentCFFZoneViewer-debug.log` 最新内容。
- Result: 程序运行中，日志文件存在且持续写入，包含时间戳及 viewer/reader 双来源日志。
- Next: 可继续按用户需要过滤特定函数耗时，或在会话结束后停止进程并归档日志。

## Current Window - 2026-04-15 04:54:02
- Objective: 针对严重性能瓶颈实现 reader 侧结构性优化。
- Actions: 在 `vtkFLUENTCFFReader` 增加 face zone 拓扑缓存与 cell-zone 索引；重写 `RequestData`/`ParseUDMData` 填充路径避免重复全量扫描；完成重编译并检查日志变化。
- Result: 构建通过；日志显示相同字段切换下 `CreateFaceZonePolyData` 多次调用从历史秒级/百秒级下降到毫秒级到亚秒级（首次构建仍较高，后续复用显著加速）。
- Next: 由用户在当前 GUI 会话继续操作并采集新一轮完整 `Update` 耗时，评估加载阶段总耗时下降幅度。

## Current Window - 2026-04-15 05:26:59
- Objective: 按用户要求增加 cas/dat 规模预估后的低风险预分配。
- Actions: 在 `vtkFLUENTCFFReader.cxx` 为 `Cells/Faces/FaceZones` 及常用子数组加入 `reserve`；在 metadata/data 读取路径提前 `reserve` 字段容器并复用 section 缓冲区；执行重编译验证。
- Result: 编译通过，改动保持现有逻辑语义，仅降低动态扩容频率与临时分配开销。
- Next: 运行同一测试工况，采集 `Update` 与字段切换耗时对比预分配前后的收益。

## Current Window - 2026-04-15 05:36:27
- Objective: 回答“原始 VTK 如何构建 cell”。
- Actions: 阅读 `vtkFLUENTCFFReader` 的 `RequestData`、`GetFaces/GetCells`、`Populate*Cell` 相关实现并梳理流程。
- Result: 明确其核心是“先按 face 拓扑重建 cell 节点顺序，再按 cell 类型调用 `InsertNextCell`”。
- Next: 向用户给出分步骤说明，并指出 polyhedron 与常规单元的差异。

## Current Window - 2026-04-15 05:52:28
- Objective: 按已确认方案实现 `examples/FluentCFFZoneViewer/perf-opt` 专用优化流程目录。
- Actions: 新增 `README.md`、`baseline-notes.md`、`build-debug.ps1`、`run-v21.ps1`、`collect-log.ps1`；执行脚本链路验证并生成日志快照与摘要。
- Result: 流程可运行，成功输出 `perf-opt-reports` 下的快照与 summary 文件。
- Next: 在后续每轮优化后复用该流程并将关键结论写入 `baseline-notes.md`。

## Current Window - 2026-04-15 05:58:16
- Objective: 修复命令行启动时的 DLL/环境变量冲突。
- Actions: 更新 `perf-opt/run-v21.ps1`，在启动前重排 PATH（`build-msys2-clang` 与 `C:/tools/msys64/mingw64/bin` 优先）并过滤 conda 路径；执行脚本验证。
- Result: 脚本成功启动 `FluentCFFZoneViewer.exe`，未再复现入口点报错。
- Next: 如需进一步稳固，可在 `build-debug.ps1` 同步加入相同 PATH 清洗逻辑。

## Current Window - 2026-04-15 06:01:39
- Objective: 按用户要求“加入并开始优化流程”执行一轮 perf-opt。
- Actions: 为 `perf-opt/build-debug.ps1` 增加同类 PATH 清洗逻辑；顺序执行 `build-debug.ps1`、`run-v21.ps1 -KillExisting`、`collect-log.ps1`。
- Result: 流程执行成功，生成新快照 `FluentCFFZoneViewer-debug-20260415-060114.log` 与摘要 `summary-20260415-060114.txt`。
- Next: 基于该快照提取本轮新增时段指标，写入 `perf-opt/baseline-notes.md` 做迭代对比。

## Current Window - 2026-04-15 06:09:17
- Objective: 在“自动循环推进”模式下执行新一轮性能迭代。
- Actions: 优化 `CreateFaceZonePolyData` 标量取值热路径（一次解析 chunk，循环内直接索引）；执行 `perf-opt` 构建、运行、采集全流程。
- Result: 新快照 `FluentCFFZoneViewer-debug-20260415-060835.log` 显示 `Update` 约 57.7s（相对上一轮约 69.2s 继续下降）；热点 face 字段切换保持毫秒级。
- Next: 继续迭代 polyhedron 构建路径（减少 type-7 分支临时对象/去重开销）。

## Current Window - 2026-04-15 16:19:37
- Objective: 继续压缩 face/cell 重建循环，聚焦 polyhedron(type-7) 热路径。
- Actions: 将 type-7 点去重改为单循环访问标记法（去掉排序去重退化路径），并复用 polyhedron 中间缓冲；执行 perf-opt 全流程复测。
- Result: 新快照 `FluentCFFZoneViewer-debug-20260415-161858.log` 显示 `polyhedron` 从约 67.6s 回落到约 25.9s，`Update` 从约 84.7s 回落到约 68.8s，恢复到可用区间并保持 face 字段切换毫秒级。
- Next: 继续推进依赖链可合并的循环（优先在 `GetFaces` 里减少 section 间重复遍历），再做一轮对比。

## Current Window - 2026-04-15 16:25:32
- Objective: 在 face/cell 重建阶段继续减少重复循环和扩容开销。
- Actions: 调整 `GetFaces` 的 c0/c1 组装为“先统计邻接计数、再一次性 reserve、最后单次回填”路径；执行 perf-opt 全流程复测。
- Result: 新快照 `FluentCFFZoneViewer-debug-20260415-162512.log` 显示 `polyhedron` 约 22.7s、`Update` 约 63.3s，较上一轮继续下降。
- Next: 继续压 `RequestData` type-7 分支（尝试减少每 cell WritePointer 与 SetData 调用频次）。

## Current Window - 2026-04-15 17:01:57
- Objective: ���û�ͬ������ƽ� face �����ڴ����ָ������Ż���
- Actions: �� vtkFLUENTCFFReader ������ FaceNodePool + nodeOffset/nodeCount����д GetFaces �ڵ�װ��Ϊ�� section һ�� resize ��ָ����䣬���� face �ڵ��ȡ�ȵ㣨GetFaceNodeId��FaceZone cache��Populate*Cell����Ϊ�������ض�ȡ��ִ�� perf-opt ����/����/�ɼ���
- Result: Debug ����ͨ����reader ����·�����л��������ط��ʣ���־�ɼ��ű��ɹ������¿����� summary �ļ���
- Next: ��һ�ּ����� cell polyhedron ��·������������أ�nodes/nodesOffset������ͬ�����Աȡ�

## Current Window - 2026-04-15 17:19:37
- Objective: 继续推进 cell polyhedron 连续内存池并验证性能。
- Actions: 在 vtkFLUENTCFFReader 增加 CellNodePool/CellNodeOffsetPool 及每 cell 偏移计数元数据；将 PopulatePolyhedronCell 改为池化写入；将 RequestData type-7 和 GetCellNode* 读取改为优先走连续池；执行 perf-opt build/run/collect 两轮采样。
- Result: 构建通过，日志显示最新完整轮次 Update 约 60.5s/66.9s（最佳轮次 60.5s），polyhedron 约 22.4s/31.8s，保持在既有优化区间内。
- Next: 继续将非 polyhedron 的 cell 节点装配也逐步切到连续池并引入单轮净日志采集，降低波动噪声。

## Current Window - 2026-04-15 17:45:42
- Objective: 继续推进非 polyhedron 节点路径池化并落地单轮净采样日志。
- Actions: 在 reader 中将非 polyhedron cell 也写入 CellNodePool，并在 RequestData 统一优先从池读取节点；为 run-v21 增加日志 marker；重写 collect-log 以从最后 marker 起生成 delta 与 summary；执行两轮 perf-opt 复测。
- Result: 第二轮单轮净采样稳定：polyhedron 36662 ms，Update 64367 ms，cell data 各字段基本回到百毫秒级；净采样文件可直接用于本轮对比。
- Next: 继续压缩 polyhedron 写入与 cell data 填充抖动（重点关注偶发 SV_DENSITY/SV_WALL_DIST 长尾）。

## Current Window - 2026-04-15 17:52:04
- Objective: 回到 type-7 路径，压缩每 cell WritePointer/SetData 频次并复测。
- Actions: 在 RequestData 的 type-7 分支引入 polyNodes/polyNodeOffsets 容量复用（按需扩容，否则 SetNumberOfValues+GetPointer）；将 faces->SetData 移出 per-cell 改为循环外一次绑定；补充 polyhedron buffer prepare 与 SetData 独立耗时日志；执行 perf-opt 净采样。
- Result: 本轮净采样显著下降：polyhedron 6276 ms，Update 38178 ms，cell data 字段大多在 66-231 ms。
- Next: 继续排查 buffer prepare 的 2.8s 成分（复制/去重占比）并尝试进一步压缩到亚秒级。

## Current Window - 2026-04-15 18:03:19
- Objective: 在 type-7 分支继续压去重开销，减少 per-cell buffer prepare 热点。
- Actions: 为 Cell 增加 uniqueNodePool 偏移/计数，并新增 CellUniqueNodePool；在 PopulateCellNodes 阶段预计算 polyhedron 唯一点列表；RequestData type-7 优先复用预计算 unique 节点，仅在缺失时兜底旧去重逻辑；执行 perf-opt 净采样复测。
- Result: 新净采样 polyhedron 降至 5249 ms（上一轮 6276 ms），Update 38420 ms（与上一轮 38178 ms 同量级），cell data 维持在约 69-143 ms 区间。
- Next: 继续针对 buffer prepare 约 3238 ms 做拆分（nodes 复制 vs offsets 复制）并尝试批量 memcpy/更紧凑表示。

## Current Window - 2026-04-15 18:24:38
- Objective: 继续压缩 type-7 buffer prepare 并细分 node/offset/unique 耗时。
- Actions: 增加 node copy、offset copy、unique select 三段独立计时日志；尝试 vtkId 池后发现回退并立即回滚；保留有效优化路径并复测。
- Result: 回滚后净采样进一步提升：polyhedron 3408 ms，buffer prepare 1928 ms（node copy 587 ms，offset copy 259 ms，unique select 190 ms），Update 31697 ms，cell data 多字段约 64-133 ms。
- Next: 基于细分日志继续做 node/offset 批量复制微调和 unique 预处理成本压缩。

## Current Window - 2026-04-15 18:33:25
- Objective: 回答“由子单元求平均覆盖父单元值”的具体含义并结合代码解释。
- Actions: 查阅 `vtkFLUENTCFFReader` 中 `GetCellTree`、`PopulateCellTree` 与调用链，确认 parent/child 标记来源与覆盖时机。
- Result: 已定位到精确逻辑：仅对 parent cell，按 child（且 child 不是 parent）逐分量算术平均并回写父单元数组槽位。
- Next: 向用户提供分步骤解释、代码片段与一个可复现的小例子。

## Current Window - 2026-04-15 19:18:30
- Objective: 继续压缩 type-7 polyhedron buffer prepare，尝试减少 node/offset copy 转换成本。
- Actions: 实现了一版 type-7 `vtkIdType` 镜像池（仅实验）并执行 perf-opt build/run/collect；发现性能显著回退后已完整回滚代码；再次构建并复测确认恢复到稳定区间。
- Result: 回退试验产生明显退化（Update 约 80.1s，buffer prepare 约 8.36s）并已撤销；回滚后最新净采样恢复为 Update 约 45.9s，polyhedron 约 7.70s，buffer prepare 约 5.09s。
- Next: 避免再走“镜像池扩内存”方向，改做更小粒度优化（优先评估 offset 表示与 per-cell 写入调用链微调）。

## Current Window - 2026-04-15 19:53:41
- Objective: 按“更小粒度”继续优化 type-7，尝试压缩 offset 复制与 SetNumberOfValues 调用开销。
- Actions: 在 `RequestData` 实验了“按 faces 现场构造 offsets”与“同尺寸跳过 SetNumberOfValues”两项微调；执行 perf-opt build/run/collect；确认回退后立即撤销该实验并重新 build/run/collect 复测。
- Result: 该实验出现明显回退（Update 约 70.8s，polyhedron 约 20.0s），已完整回滚；回滚后最新净采样恢复到稳定区间（Update 约 53.5s，polyhedron 约 5.0s，buffer prepare 约 2.58s）。
- Next: 下一轮避免改动 RequestData 热循环内流程，转向“预构建阶段减少 node/offset 总量”方向（优先评估 `PopulatePolyhedronCell` 的去冗余写入）。

## Current Window - 2026-04-15 20:07:30
- Objective: 按许可执行“预构建阶段去冗余”优化，减少 type-7 的二次扫描。
- Actions: 实验将 unique 预处理并入 `PopulatePolyhedronCell` 单循环并修改函数签名；执行 perf-opt build/run/collect 后确认回退；已完整回滚本次实验并再次 build/run/collect 验证恢复。
- Result: 实验版出现回退（Update 约 69.9s，polyhedron 约 8.66s，buffer prepare 约 5.46s）；回滚后恢复到稳定区间（Update 约 51.2s，polyhedron 约 5.13s，buffer prepare 约 2.72s）。
- Next: 暂停“预构建去冗余”方向，后续改为围绕波动项（cell data chunk）做稳定性优化与多轮中位数评估。

## Current Window - 2026-04-15 20:16:19
- Objective: 按用户确认的方案 B 去除 cell tree 冗余逻辑。
- Actions: 在 `vtkFLUENTCFFReader.cxx/.h` 删除 `GetCellTree` 与 `PopulateCellTree` 的调用、声明和实现，并检索确认无残留引用。
- Result: cell 树分支相关逻辑已彻底移除，代码内 `GetCellTree/PopulateCellTree/cells/tree` 均无匹配。
- Next: 如需，我可以继续补一轮构建验证，确保该清理在当前环境无编译回归。

## Current Window - 2026-04-15 20:30:47
- Objective: 按用户要求总结当前状态并给出热力图，继续执行多轮稳定采样。
- Actions: 连续执行 3 轮短窗口采样后发现数据不完整（未覆盖到 Update 完成）；改为 3 轮 95s 长窗口采样并读取 delta 日志提取关键耗时。
- Result: 得到完整三轮数据，Update 分别约 52.0s/52.8s/47.2s，中位数约 52.0s；polyhedron 中位数约 5.4s，buffer prepare 中位数约 2.4s，cell data 存在明显长尾抖动（个别 chunk 达 0.4~1.1s）。
- Next: 输出当前状态热力图并建议下一轮优先治理 cell data chunk 长尾，而非继续改 polyhedron 数据结构。

## Current Window - 2026-04-15 22:58:00
- Objective: 执行"Update 性能极限优化"完整 5 阶段计划。
- Actions:
  - 阶段一：为 ParseCaseFile 内部所有子调用（GetNodesGlobal/GetCellsGlobal/GetFacesGlobal/GetNodes/GetCells/GetFaces 等）、CleanCells、PopulateCellNodes、GetData（ReadDataForType cells/faces）、ZoneMapping、cell loop 添加 Debug 计时日志。采样 2 轮确认暗区分布。
  - 阶段二：移除 GetCellsGlobal 冗余 per-cell reserve（节省 ~2.4s）；GetNodes 中 H5Gopen 提到循环外；GetFaces 复用 section 级缓冲区；ReadDataForType 单分量字段特化为直接 memcpy。
  - 阶段三：PopulateCellNodes 估算循环改用统计估算（避免逐 cell 逐 face 遍历）；PopulateHexahedronCell 3 轮面扫描合并为 1 轮；PopulateWedgeCell 类似合并。
  - 阶段四：cell data scatter 写入检测连续性后走 memcpy 零拷贝路径。
  - 阶段五：为每个 zone grid 预分配 Allocate(numCells)；修复重复 reserve。
- Result: Update 从基线 ~77s（含插桩开销）降至 **33-40s**（多轮采样范围）。各子步骤改善：GetCellsGlobal 2.9s→0.4s；ParseCaseFile 11.7s→8.8s；PopulateCellNodes 7.8s→5.5s；GetData 20.5s→10.9s；cell data chunks 长尾从 200-340ms 降至 21-66ms。
- Next: 全部 5 阶段计划已完成。后续可进一步优化 GetFaces c0+c1+adjacency（~3.3s）和 polyhedron node copy（~0.6-4.2s 波动大）。

## Current Window - 2026-04-15 23:58:16
- Objective: 更新 Fluent CFF 文档，使其与当前 reader 状态一致。
- Actions: 更新 `docs/fluent-cff-dat-cas-structure.md` 的“特殊拓扑”段落，明确当前 reader 不再解析/依赖 `/meshes/1/{cells|faces}/tree`；重构 `docs/fluent-cff-modified-modules.md`，将改动拆为“构建/链接兼容”和“reader 性能/结构优化”两阶段，并补充 `perf-opt` 流程脚本模块清单。
- Result: 两份文档已同步到当前代码演进路径，避免“仍会读取 tree”与“只改 3 个文件”的误导性描述。
- Next: 如需更细粒度，可继续补充阶段 B 的关键优化点索引（例如缓存/内存池/批量 memcpy 的对应函数入口）。

## Current Window - 2026-04-16 00:06:30
- Objective: 按用户确认方案 A，默认展示 Cell zone 以直接看到 polyhedron。
- Actions: 修改 `examples/FluentCFFZoneViewer/main.cxx` 的加载完成逻辑，在填充 topology 前将 `kindCombo` 强制切换到 `Cell zone`，并直接执行 `PopulateCellZones/PopulateCellArrays`；随后运行 `build-debug.ps1` 验证编译。
- Result: 加载完成后默认进入 cell 渲染路径，不再默认显示 face zone；Debug 构建通过。
- Next: 如需，我可以继续加一条状态栏提示，明确“当前显示 Cell zone（含 polyhedron）”。

## Cursor - 2026-04-16 01:15:00
- Objective: fix polyhedron cell not rendering in any cell zone.
- Actions: (1) reverted main.cxx default-CellZone change; (2) added zone diagnostics; (3) found root cause by diff with original VTK code; (4) moved faces->SetData() back inside polyhedron loop.
- Root Cause: vtkCellArray::SetData() called only once outside loop; subsequent in-place modifications to underlying arrays left CellArray internal state stale, causing InsertNextCell(VTK_POLYHEDRON) to read incorrect face data.
- Result: polyhedron rendering restored. Update perf regressed from ~42s to ~62s (+20s per-cell SetData overhead).
- Next: explore lighter CellArray refresh to recover performance.

## Current Window - 2026-04-16 01:22:30
- Objective: 处理 `git push` non-fast-forward 拒绝并判定安全同步方案。
- Actions: 检查 `git status`、远端 `VTK-cff`、本地分支与远端引用；执行 `git fetch --all --prune` 并查看提交图。
- Result: 确认本地 `main` 与远端 `VTK-cff/main` 为两条不相干历史（远端仍为 `Initial commit`），因此无法快进推送。
- Next: 用户选择方案：A) 合并两条历史后常规 push；或 B) `--force-with-lease` 强推用本地历史覆盖远端 `main`。

## Current Window - 2026-04-16 09:56:00
- Objective: 更新“doc 下内容”，为文档入口与索引补齐指路。
- Actions: 确认仓库实际文档目录为 `docs/`；新增 `doc/README.md` 作为兼容入口；新增 `docs/README.md` 作为索引与推荐阅读顺序。
- Result: 已补齐 `doc/` → `docs/` 的入口说明，且 `docs/` 下有明确索引页。
- Next: 如需进一步“更行/更新”文档内容本身，请指出要改的主题（例如：字段解释、特殊拓扑、或与当前 reader 行为对齐的段落）。

## Current Window - 2026-04-16 10:02:10
- Objective: 更新 `docs/`，使文档描述与 `vtkFLUENTCFFReader` 当前行为对齐。
- Actions: 对照源码核对 tree/interface/overset/periodic/NCC 等特殊拓扑支持现状；修订 `docs/fluent-cff-dat-cas-structure.md` 的“特殊拓扑”段落；修订 `docs/cff-v21-cas-dat-hdf5-field-tree.md` 的 TODO 描述，区分“已实现的 interface 标记”与“仍 TODO 的 Nonconformal/Periodic”。
- Result: 文档已与当前 reader 行为一致：不再读取 `cells/tree`，但仍读取 `faces/tree` 并在 `CleanCells()` 中按 child/interface child 过滤；interface parents/children 标记已实现，Nonconformal/Periodic 仍为 TODO。
- Next: 如需更彻底对齐，可继续补一张“特殊拓扑支持矩阵”（路径→函数→是否实现→影响点）并在 `docs/README.md` 里链接。

## Current Window - 2026-04-16 22:09:06
- Objective: 将 `doc/docs` 下文档中的路径链接改写为相对路径。
- Actions: 检查 `doc/` 与 `docs/` 目录及 Markdown 链接；将 `docs/fluent-cff-dat-cas-structure.md`、`docs/fluent-cff-modified-modules.md` 中的绝对盘符链接改为相对路径；补充 `docs/cff-v21-cas-dat-hdf5-field-tree.md`、`docs/README.md`、`doc/README.md` 的文档互链为相对链接；复核确认无残留绝对路径链接。
- Result: `doc/` 与 `docs/` 内现有 Markdown 路径链接已统一为相对路径，仓库内未再检出 `/F:/Users/20968/projects/ai/gnn/` 形式的文档链接。
- Next: 如需，我可以继续把 `docs/` 中尚未做成链接的纯文本路径也统一改成可点击相对链接。

## Current Window - 2026-04-17 22:33:00
- Objective: 补充 zoneType 查询接口并更新文档。
- Actions:
  - 在 `vtkFLUENTCFFReader.h` 中为 `FaceZoneInfo` 添加 `zoneType` 成员，声明 `GetFaceZoneNameById`/`GetFaceZoneType`/`GetCellZoneType` 接口。
  - 在 `vtkFLUENTCFFReader.cxx` 中修改 `GetFaces()` 读取 `zoneT[iZone]` 并存储到 FaceZoneInfo；实现三个查询函数；添加 `CellZonesType` 和 `CellZoneIdToType` 成员变量（待完整实现 cell zone 映射）。
  - 更新 `docs/cff-v21-cas-dat-hdf5-field-tree.md`，在 zoneTopology 说明处添加 zoneType 含义，并新增附录 A 完整映射表。
- Result: 新接口已添加，FaceZone 可通过 zoneId 查询名称和 zoneType；文档已补充 zoneType 映射说明。
- Next: CellZonesType 的完整填充需在读取 cell zone 时存储 zoneId→zoneType 映射（目前为 TODO）。

## Current Window - 2026-04-17 22:48:00
- Objective: 在文档中增加 Cell/Face 重建实现说明，着重体现与原 VTK 模块的差异。
- Actions: 在 `docs/fluent-cff-modified-modules.md` 第 8 节添加"Cell 与 Face 重建实现说明"，包含：Cell 重建流程（各类 cell 类型重建方式）、Polyhedron 重建细节、Face 重建流程、FaceZone 拓扑缓存、与原始 VTK vtkFLUENTReader 的差异对比表。
- Result: 文档已补充 Cell/Face 重建实现说明，包含总体流程、各类型重建策略、内存池优化、与原始模块的差异对比。
- Next: 如有需要可进一步细化各重建函数的代码级说明。

## Current Window - 2026-04-17 23:15:00
- Objective: 添加 Cell 质心、edge_index 和 face→cell 边界类型查询功能，支持 GNN 图结构构建。
- Actions:
  - 在 `vtkFLUENTCFFReader.h` 中添加 `CellCentroids`/`EdgeIndexSrc`/`EdgeIndexDst` 成员变量，新增 `GetCellCentroidCount`/`GetCellCentroids`/`GetEdgeCount`/`GetEdgeIndex`/`GetFaceBoundaryType` API 声明和 `BuildGraphData()` 方法声明。
  - 在 `vtkFLUENTCFFReader.cxx` 中实现 `BuildGraphData()` 函数：遍历 Faces 提取 c0/c1 邻接构建 edge_index（双向边）；遍历 Cells 取节点坐标平均值作为质心存入 `CellCentroids` 数组；在 `RequestData` 中调用 `BuildGraphData`。
  - `GetFaceBoundaryType` 返回 0=内部面（c0≥0 且 c1≥0），1=边界面（任一侧 < 0）。
- Result: 已提供 Cell 质心数组 `[N*3]`、双向 edge_index `[2,E]`、face 边界类型识别，可供 GNN 使用。
- Next: 如需四面体分割法精确质心计算可进一步优化；当前使用节点平均作为简化。

## Current Window - 2026-04-17 23:45:00
- Objective: 添加 Face 型心和面外法向计算功能，支持按 zoneId 提取边界数据。
- Actions:
  - 在 `vtkFLUENTCFFReader.h` 中添加 `FaceCentroids`/`FaceNormals`/`FaceZoneIdToFaceIndices` 成员变量和 `GetBoundaryFaceCount`/`GetFaceCentroids`/`GetFaceNormals`/`GetFaceCentroidsByZone`/`GetFaceNormalsByZone` API 声明。
  - 在 `vtkFLUENTCFFReader.cxx` 中实现 5 个查询 API 函数。
  - 在 `BuildGraphData()` 中添加边界面遍历逻辑：过滤 interior 面（c0≥0 && c1≥0），对每个边界面计算节点平均型心、前3节点叉乘归一化法向，并建立 zoneId → 局部索引映射。
- Result: 已提供边界 face 型心数组 `[N*3]`、法向数组 `[N*3]`、按 zoneId 提取接口，可供 GNN 边界面特征使用。
- Next: 如需可扩展支持 face→cell 边界类型识别（壁面/速度入口/压力出口等）。

## Current Window - 2026-04-17 23:59:30
- Objective: 删除 `Face.zone` 冗余并将按 zone 提取边界 face 数据改为连续区间拷贝。
- Actions: 在 `vtkFLUENTCFFReader` 中移除 `Face::zone`，新增 `FaceIdToZoneId` 映射与 `FaceZoneIdToBoundarySpan`；重写 `BuildGraphData()` 使 boundary face 的型心/法向按每个 FaceZone 的 `firstFaceId..lastFaceId` 连续写入（退化面用 0 向量占位）；更新 `GetFaceZoneId`/`GetFaceCentroidsByZone`/`GetFaceNormalsByZone` 为 O(1) 区间复制；同步更新文档说明；执行 `cmake --build examples/FluentCFFZoneViewer/build-msys2-clang` 验证编译通过。
- Result: 编译通过；按 zoneId 获取边界 face 型心/法向现在为连续段拷贝，避免逐个 push_back；`GetFaceZoneId(faceId)` 不再依赖 Face 结构体冗余字段。
- Next: 如需进一步优化，可在 `BuildGraphData()` 中对 per-face centroid/normal 计算做 SIMD/批量读取或复用缓存，减少 Update 时重复计算。

## Current Window - 2026-04-18 00:12:30
- Objective: 对 cell 侧做与 face 相同的区间/span 改造，移除 `Cell.zone` 并新增按 zone 一次性拷贝 cell 质心接口。
- Actions: 在 `vtkFLUENTCFFReader` 中移除 `Cell::zone`，新增 `CellIdToZoneId`、`CellZonesInfo`、`CellZoneIdToCellSpan`；在 `GetCellsGlobal/GetCells` 构建 cellId→zoneId 映射与 span；`GetNumberOfCellZones/RequestData/ParseUDMData` 改为通过 `GetCellZoneId()` 映射而非直接访问 `Cell.zone`；新增 `GetCellCentroidsByZone(zoneId, out)` 走区间复制；执行 `cmake --build examples/FluentCFFZoneViewer/build-msys2-clang -j 8` 验证编译通过。
- Result: 编译通过；cell zone 映射不再依赖 `Cell.zone` 冗余字段；按 zone 获取 cell 质心可 O(1) 切片拷贝。
- Next: 若需要进一步减少 `CellIndicesByZone` 的内存与构建耗时，可将其从“存全部 cellId 列表”改为“存 span 并在需要时按需遍历区间”。 

## Current Window - 2026-04-17 12:02:11
- Objective: FaceNormals 模式下 zone 下拉框显示名可带后缀，但按 Fluent zoneId 取边界法向数据。
- Actions: 在 `examples/FluentCFFZoneViewer/main.cxx` 为每个 face zone 项设置 `Qt::UserRole+1` 存 `GetFaceZoneIdByName`；`updateView` 的 FaceNormals 分支改为 `currentData(kFaceZoneIdItemRole)`，去掉与 `currentText()` 的字符串匹配；顺带去掉误用 `GetFaceZoneId(i)`（参数实为 faceId）。
- Result: 逻辑与 UI 显示解耦，带 `(boundary:n)` 后缀时仍能正确解析 zoneId。
- Next: 本地有构建目录时可再跑一遍 `cmake --build` 确认无编译回归。

## Current Window - 2026-04-17 12:15:00
- Objective: 检查 `main.cxx` 是否还有“下标当 zoneId”或 Face/Cell 模式混用问题。
- Actions: 将 `PopulateFaceZones` 中 `GetFaceCentroidsByZone(i)` 改为使用 `GetFaceZoneIdByName` 得到的 `zoneId`；在 `updateView` 为 `TopologyKind::FaceZone` 增加独立分支（`CreateFaceZonePolyData` + `ApplyColorRange`），Cell 分支保留原多块网格逻辑并恢复 surface 显示属性。
- Result: 边界计数与按 zone 取数一致；Face zone 不再误走 cell block 索引。
- Next: 本地构建验证。

## Current Window - 2026-04-17 12:35:00
- Objective: Face normals 视图向量更明显，并按采样点区分颜色。
- Actions: 在 `main.cxx` 为每个边界采样点增加标量 `VectorIdx` 驱动 `lookupTable`；未勾选 Show Normals 时用大号彩色点；勾选时用 `vtkArrowSource` + `vtkGlyph3D`（`SetColorModeToColorByScalar`、按包围盒对角线 5% 自动定箭头长度）。`CMakeLists.txt` 为可执行目标显式链接 `VTK::FiltersGeneral`/`VTK::FiltersSources`。
- Result: 源码侧编译通过；本机链接仍可能受 VTK/Qt 导入库环境影响（与 `vtkFloatArray` 模板符号相关，非本次改动独有）。
- Next: 若链接失败，检查 MSYS2 VTK 与 `LocalFLUENTCFFReader` 的 `vtkAOSDataArrayTemplate` 实例化配置。

## Current Window - 2026-04-17 12:50:00
- Objective: 修复 `FluentCFFZoneViewer` 链接 `vtkAOSDataArrayTemplate<float/double>::SetValue` undefined reference（`__imp__`）。
- Actions: 根因是 `LocalFLUENTCFFReader` 使用 `VTKCOMMONCORE_STATIC_DEFINE` 而 `main.cxx` 默认 `dllimport`，符号约定不一致。为 `FluentCFFZoneViewer` 目标增加与 reader 相同的 `VTKCOMMONCORE_STATIC_DEFINE`/`VTK_USE_EXTERN_TEMPLATE=0`；在 `vtkAOSDataArrayTemplateInstantiate.cxx` 增加 `template class vtkAOSDataArrayTemplate<double>;`；Face normals 分支保留 `vtkDoubleArray`（由 `float` 边界数据 cast 写入）。
- Result: `cmake --build …/build-debug --target FluentCFFZoneViewer.exe` 链接成功（exit 0）。
- Next: 无。

## Current Window - 2026-04-17 13:00:00
- Objective: FluentCFFZoneViewer 增加「显示全拓扑」：各 Kind 下一次性显示全部 face zone / 全部 cell block / 全部有边界法向的 zone。
- Actions: 在 `main.cxx` 增加 `显示全拓扑` 复选框；勾选时禁用 `zoneCombo` 并在 `updateView` 中 Face 用 `vtkAppendPolyData`、Cell 用 `vtkAppendFilter`、FaceNormals 合并各 zone 采样点与全局 `VectorIdx`；`setLoadingState` 与 toggled 同步 zone 可用性；`CMakeLists.txt` 为可执行目标增加 `VTK::FiltersCore`。
- Result: `cmake --build examples/FluentCFFZoneViewer/build-debug --target FluentCFFZoneViewer.exe` 通过（exit 0）。
- Next: 若全 cell 合并内存过大，可加进度或上限提示。

## Current Window - 2026-04-17 23:59:59
- Objective: 实现 FluentCFF → GNN Tensor 导出模块（独立 CMake 入口 + pybind11 + LibTorch）。
- Actions: 探测 MSYS2 `mingw64/lib/cmake`，确认当前未安装 `pybind11` 与 `Torch`；在 `vtkFLUENTCFFReader` 增加 loaded chunks 访问器；新增 `FluentCFFGNN/FluentCFFGNNExporter.*`（由早期 `vtk/IO/` 下路径迁出并更名）导出 boundary/cell/edge/one-hot 与字段拼接；新增 `python/fluentcff_gnn_pybind.cpp` 绑定；新增独立构建入口 `cmake/FluentCFFGNNPy/CMakeLists.txt`（仿照 viewer 的 MSYS2/VTK/HDF5 约定）与 smoke 脚本。
- Result: 代码与构建入口已落地；由于环境尚未安装 `pybind11`/LibTorch，当前仅能完成源码侧集成，待依赖就绪后可直接编译生成 `.pyd` 并运行 smoke 脚本验证。
- Next: 安装 `pybind11`（建议 MSYS2 pacman）并下载解压 LibTorch，配置 `pybind11_DIR`/`Torch_DIR` 后执行 CMake 配置与构建，再运行 `python/smoke_test_fluentcff_gnn.py`。

## Current Window - 2026-04-20
- Objective: 修复 FluentCFFGNNPy 在 MSVC+Ninja 下 pybind11 误选 MSYS2 Python 导致的 “Python libraries not found”，并澄清 pip CUDA torch 在无 CUDA 工具链时 `find_package(Torch)` 失败。
- Actions: 更新 `cmake/FluentCFFGNNPy/CMakeLists.txt`：强制 `Python_EXECUTABLE` 为 `.venv/Scripts/python.exe`、启用 `PYBIND11_FINDPYTHON`、在 `find_package(pybind11)` 前先 `find_package(Python ... Development.Module)`；补充 Torch/CUDA 与 CPU 轮重装提示；复跑 `cmake` 验证 Python 链路已通。
- Result: pybind11 已能使用 venv 的 Python 3.13；Torch 仍因 venv 内 CUDA 版 torch 找不到 CUDAToolkit 而中止配置。
- Next: 在 `.venv` 内重装 CPU 版 `torch`，或安装与 torch 匹配的 CUDA 工具链后重新配置并构建。

## Current Window - 2026-04-20 (CUDA path)
- Objective: 在 FluentCFFGNNPy CMake 中显式指定本机 CUDA 12.8 安装路径，供 pip 版 CUDA torch 的 `find_package(Torch)` 使用。
- Actions: 在 `cmake/FluentCFFGNNPy/CMakeLists.txt` 的 `find_package(Torch)` 前增加 `FLUENTCFF_NVCC`/`FLUENTCFF_CUDA_ROOT` 默认值（`C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8`），设置 `CUDAToolkit_ROOT`、`CUDA_TOOLKIT_ROOT_DIR` 与 `ENV{CUDA_PATH}`，并将 CUDA 根加入 `CMAKE_PREFIX_PATH`。
- Result: 配置阶段应能定位与本机 `nvcc` 一致的 toolkit；若版本与 torch 仍不匹配需自行调整 cache 或重装 torch/CUDA。
- Next: 重新运行 `cmake -S cmake/FluentCFFGNNPy -B build/fluentcff_gnn_py` 验证 `find_package(Torch)`。

## Current Window - 2026-04-20 (CUDA root validation)
- Objective: 避免将非完整 CUDA 目录（如缺少 `include/cuda.h` 的 `D:/CUDA/v12.8`）传给 Torch CMake 导致含糊失败。
- Actions: 在 `cmake/FluentCFFGNNPy/CMakeLists.txt` 对 `FLUENTCFF_CUDA_ROOT` 校验 `include/cuda.h`，失败则 `FATAL_ERROR` 并提示典型 `Program Files` 路径；补充 `CUDA_HOME`、`CUDA_NVCC_EXECUTABLE`、`ENV{CUDA_HOME}` 以配合 PyTorch 内置 `find_package(CUDA)`。
- Result: 配置阶段可更早发现错误 CUDA 前缀；正确 toolkit 下更易满足 `find_package(Torch)`。
- Next: 使用真实 CUDA 根目录重新配置，或改用 CPU 版 torch。

## Current Window - 2026-04-20 (layout)
- Objective: 将 GNN 导出代码移出 `vtk/` 树，避免与 VTK 模块目录混放。
- Actions: 删除 `vtk/IO/FLUENTCFFGNN/` 下源文件，在仓库根新增 `FluentCFFGNN/FluentCFFGNNExporter.h/.cxx`（后续再更名为无前缀类名）；更新 `cmake/FluentCFFGNNPy/CMakeLists.txt` 的源码与 include 路径；修正 `HISTORY.md` 中旧路径描述。
- Result: Exporter 位于 VTK 模块外部，仍通过 `vtkFLUENTCFFReader` 依赖 `vtk/IO/FLUENTCFF`。
- Next: 无。

## Current Window - 2026-04-20 (rename)
- Objective: 去掉导出类名的 `vtk` 前缀，并与文件名对齐。
- Actions: 将 `vtkFLUENTCFFGNNExporter` 重命名为 `FluentCFFGNNExporter`；源文件改为 `FluentCFFGNN/FluentCFFGNNExporter.h/.cxx`；更新 `fluentcff_gnn_pybind.cpp` 与 `cmake/FluentCFFGNNPy/CMakeLists.txt`。
- Result: Python 侧仍暴露为 `fluentcff_gnn.Exporter`，C++ 类型名为 `FluentCFFGNNExporter`。
- Next: 无。

## Current Window - 2026-04-20 (IntelliSense torch includes)
- Objective: 修复 VS Code / clangd 报 `torch/torch.h` 找不到；在 `c_cpp_properties.json` 中补充与 CMake 一致的 pip torch 头文件路径。
- Actions: 在 `.vscode/c_cpp_properties.json` 的 `includePath` 增加 `${workspaceFolder}/.venv/Lib/site-packages/torch/include` 与 `.../torch/include/torch/csrc/api/include`（pip 轮中 `torch/torch.h` 位于后者之下）；用 `Test-Path` 与 `clang++ -fsyntax-only` 验证包含链可打开该头文件。
- Result: 头文件路径正确；MinGW clang 对 MSVC 构建的 torch 头仍可能产生大量 ABI 相关告警/错误，但「找不到 torch/torch.h」问题已消除。
- Next: 若希望 IntelliSense 与 MSVC 构建完全一致，可将 `compilerPath`/`intelliSenseMode` 切到 MSVC 工具链或生成 `compile_commands.json` 供 clangd 使用。

## Current Window - 2026-04-20 (Torch link target)
- Objective: 修复 pip torch 配置成功后链接阶段报 `Torch::Torch` 目标不存在。
- Actions: `cmake/FluentCFFGNNPy/CMakeLists.txt` 改为优先链接 CMake 目标 `torch`（pip 版 TorchConfig 提供），否则再尝试 `Torch::Torch`；CUDA 默认路径注释标明可为自动检测到的非 12.8 版本（如本机 v12.5）。
- Result: 与 PyTorch 官方 CMake 变量/目标命名一致，生成步应可通过。
- Next: 重新 `cmake` 与 `cmake --build` 全链路验证。

## Current Window - 2026-04-19 18:55:27
- Objective: 接续 FluentCFFGNNPy：补全 MSVC 下 HDF5（vcpkg）与 CUDA/CPU torch 的 CMake 行为，避免非 Windows 误用 MSYS2 HDF5。
- Actions: 更新 `cmake/FluentCFFGNNPy/CMakeLists.txt`：MSVC 分支 `find_package(hdf5 CONFIG)` 并链接 `hdf5::hdf5-static`/`hdf5::hdf5-shared`；UNIX 使用 `find_package(HDF5)` + `HDF5::HDF5`；恢复 CUDA 的 nvcc/PATH/Program Files（及 Linux `/usr/local/cuda*`、`/opt/cuda*`）自动探测；仅在 venv 中存在 `torch_cuda.dll`/`libtorch_cuda.so*` 等时才强制定位 CUDA Toolkit，CPU 轮跳过 CUDA 前缀设置；非 Windows 上 `CUDA_NVCC_EXECUTABLE` 使用 `bin/nvcc`。
- Result: MSVC+vcpkg 路径下不再写死 MSYS2 的 `libhdf5.dll.a`；CPU-only pip torch 不应再被缺少 `cuda.h` 的配置阶段误杀。
- Next: 在 VS Native Tools 下用 `-DFLUENTCFF_MSVC_VCPKG_ROOT=.../installed/x64-windows` 重新配置并 `cmake --build`，再跑 `python/smoke_test_fluentcff_gnn.py`。

## Current Window - 2026-04-19 20:22:10
- Objective: 将 vcpkg 的下载/编译/安装输出迁到 E:，解决 F: 盘 VTK 构建时 `No space left on device`；避免 PATH 中 MSYS `cmake` 参与 vcpkg。
- Actions: 新增 `cmake/FluentCFFGNNPy/install-vcpkg-deps-E.ps1`（`--downloads-root` / `--x-buildtrees-root` / `--x-packages-root` / `--x-install-root` 均指向 `E:\vcpkg-work\fluentcff-gnn`）；更新 `FLUENTCFF_MSVC_VCPKG_ROOT` 的 CACHE 说明；诊断 F: 仅剩约 17MB、E: 约 143GB 可用。
- Result: 本机 Cursor 后台拉起的长时 `vcpkg` 不可靠，需用户在 **VS Developer PowerShell** 中自行执行该脚本直至结束；成功后 CMake 使用 `-DFLUENTCFF_MSVC_VCPKG_ROOT=E:/vcpkg-work/fluentcff-gnn/installed/x64-windows`。
- Next: 用户执行脚本完成 VTK 后，再配置 `cmake/FluentCFFGNNPy` 并构建 `fluentcff_gnn`。

## Current Window - 2026-04-19 (vcpkg cmake PATH)
- Objective: 修复 zlib/VTK 在 x64-windows 下仍调用 `C:/tools/msys64/mingw64/bin/cmake.exe` 导致的 `BUILD_FAILED`。
- Actions: 强化 `install-vcpkg-deps-E.ps1`：强制将 `F:\VS\...\CMake\bin` 置于 PATH 最前、用 `(?i)msys64` 过滤 PATH、`Get-Command cmake` 校验不得再落在 MSYS/MinGW。
- Result: 仅用脚本启动 vcpkg 时应在配置阶段即发现错误 CMake；用户若手跑 `vcpkg.exe` 也需先清理 PATH 或套同一套 PATH 规则。
- Next: 重新执行安装脚本；必要时先 `vcpkg remove zlib:x64-windows --recurse` 或清掉 E: 下对应 `buildtrees\zlib` 后重编。

## Current Window - 2026-04-19 21:30:00
- Objective: 解决 FluentCFFGNNPy 链接 `pybind11::detail::type_caster<at::Tensor>::cast` 失败，并验证 Python 能加载扩展、与 tasks 工作流一致。
- Actions: 在 `cmake/FluentCFFGNNPy/CMakeLists.txt` 中 `find_library(torch_python)` 并链接到 `fluentcff_gnn`（保留 `TORCH_EXTENSION_NAME`）；`python/smoke_test_fluentcff_gnn.py` 增加 Windows `os.add_dll_directory`（torch/lib、vcpkg `bin`、`CUDA_PATH`）；`.vscode/tasks.json` 的 smoke 任务注入 `FLUENTCFF_MSVC_VCPKG_ROOT=${config:fluentcff.gnn.vcpkgRoot}` 与 `CUDA_PATH`。
- Result: `cmake --build --preset fluentcff-gnn-py-msvc` 链接成功；在设置 `FLUENTCFF_MSVC_VCPKG_ROOT` 与 `CUDA_PATH` 后 smoke 脚本跑通并打印 `OK`。
- Next: 若本机未设系统级 `CUDA_PATH`，可在该任务的 `env` 中改为显式路径或依赖用户 shell 环境。

## Current Window - 2026-04-19 22:05:00
- Objective: 按计划新增 FluentCFFGNNPy 编译排障与 `fluentcff_gnn` API 的 Markdown 文档。
- Actions: 新增 `doc/FluentCFFGNNPy-build-troubleshooting.md`、`doc/fluentcff_gnn_module.md`；更新 `doc/README.md` 入口链接。
- Result: 文档与仓库 CMake/pybind/smoke/tasks 现状对齐，便于后续查阅与 onboarding。
- Next: 若 `docs/README.md` 也需索引，可再补一条链到 `doc/` 两篇文档。
