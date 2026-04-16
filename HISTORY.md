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
