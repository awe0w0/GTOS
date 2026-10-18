# 原生 Blink mojom_core 生成合同

本文件记录 `third_party/blink/public/mojom/frame/frame.mojom-forward.h` 所属 owner
`//third_party/blink/public/mojom:mojom_core` 的**原始生成合同与准备结论**。

只做合同准备与校验：**未执行生成**，`producer_qualified=false`、
`generation_executed=false`、`generation_permitted=false`。这不代表 M 的下一障碍已解决，
也不是 Content、浏览器或网页结果。

**当前状态**：合同准备与轻量校验已在第六版通过（`prepare-r6-01`，端到端 11.44 秒，
11 个案例全 PASS）。前五版分别因路径前缀比较、检查脚本绑定旧模块、历史来源查错合同等
原因失败，那些运行目录全部保留。

需要分清三件不同的事：

| 事实 | 状态 |
|---|---|
| r4 通过的准备与 8 例（旧范围，缺 ui/color mapping） | 已通过 |
| 发现遗漏：core mapping 消费 `gen/ui/color/mojom__type_mappings`，原计划未包含 | 已确认 |
| 完整新合同（2 parser + 2 mapping + 2 generator，6 action） | **第六版通过** |
| 实际生成 | **从未运行** |

本轮通过的是**合同准备与校验**，不是生成许可，也不是 Content 或浏览器结果。

## owner 与目标头

`frame/frame.mojom-forward.h` 由 `mojom("mojom_core")` 产出（`BUILD.gn:1156` 起）。
它是 `normal` 阶段的输出，不是 blink 变体：变体产出的是 `*.mojom-blink-forward.h`，
文件名与所需不符。

| 项 | 值 |
|---|---|
| owner 元数据 | `third_party/blink/public/mojom/mojom_core.build_metadata` |
| 自身 IDL | 51 个，含 `frame/frame.mojom` |
| 闭包 metadata | 121 个 |
| shared 输出 | 357 |
| normal 输出 | 255 |
| 目标 IDL 的 shared 配套 | 7 个（`-data-view.h`/`-features.h`/`-params-data.h`/`-send-validation.h`/`-shared-internal.h`/`-shared.cc`/`-shared.h`） |
| 唯一 module 引用 | 830 |
| 已可复用 | 777 |
| **真实缺口** | **53** |

## 缺口

830 个 module 中 777 个已在既有的 mojom_platform 生产里合格，可原字节复用。
其余 53 个在任何已验收运行中都不存在：

| 来源 | 数量 | 说明 |
|---|---|---|
| `mojom_core` 自身 parser 输出 | 51 | 整个 owner 的 parser action 从未执行过 |
| `//ui/color:mojom` parser 输出 | 2 | `color_id.mojom-module`、`scheme_variant.mojom-module`，属另一个 owner |

另外 `mojom_core` 有**自己的** native typemap action
（`gen/third_party/blink/public/mojom/mojom_core__type_mappings`），同样尚未产出。
platform 的 typemap 不能代替它：两者是不同 action、不同语义。

### core mapping 的输入依赖

core 原生 mapping 声明了 **29** 个 mapping 输入，其中 28 个来自已验收的 interfaces 运行
（25 个是该运行的 outputs，3 个是它的 reused 输出），**1 个**是
`gen/ui/color/mojom__type_mappings`——它在任何已验收运行中都不存在。

真实 GN 图里存在独立的 `__ui_color_mojom__type_mappings` 规则：调用
`generate_type_mappings.py`，输出该 map，读取 `gen/ui/color/mojom.typemap_config` 与
`gen/ui/color/mojom.ts_typemap_config`，**没有** `--dependency`。因此它是必须新增的
第二个 mapping 生产者，而不是可以被某个同名文件顶替的东西。

因此完整计划是 **2 个 parser + 2 个 mapping + 2 个 C++ generator，共 6 个 action**，
未来顺序为：ui/color parser → core parser → ui/color mapping → core mapping →
shared generator → normal generator。前一个 mapping 必须早于消费它的 core mapping。

预期新增产物 53 个 module + 2 个 map + 612 个绑定 = 667 个，最终以原始图的精确
outputs 为准。这份合同本身**不构成**生成许可。

## 复现入口

合同包在 `artifacts/native-content-mojom-core-20261018-a`（**不入库**）。
仓库内容不足以复现，需要包内以下脚本：

| 用途 | 包内脚本 |
|---|---|
| 合同收集与校验（第六版，已通过） | `prepare-core-contract-v6.py` |
| 同一校验函数的轻量案例（第六版） | `check-core-contract-v6.py` |
| 单子进程准备入口（第六版） | `launch-core-prepare-v6.py` |

第一至第五版的 `*-v1.py`…`*-v5.py` 全部冻结不再编辑；运行目录 `prepare-01`…
`prepare-05`、`prepare-r2-01`、`prepare-r3-01`、`prepare-r4-01`、`prepare-r5-01`、
`prepare-r6-01` 全部保留。

模块路径与预期摘要**只由启动器定义**，经 argv 传给检查脚本：`OUT`、模块绝对路径、
预期 SHA256。检查脚本不再保有任何版本文件名常量。它在导入前核对原始字节的摘要，
导入后再核对 `module.__file__` 的规范路径与文件摘要，两处都匹配才继续收集合同。

执行结构为 `wsl.exe -d Ubuntu-20.04` → `/usr/bin/env -i` 最小环境 →
`unshare --user --map-root-user --net` → `/usr/bin/python3 -I -B -S` 绝对脚本路径；
Landlock ABI 7、工作区只读、仅本次新 OUT 可写、无外网、单子进程 `shell=False`。

入口 guard 只记录真实设置：本入口**没有**安装 subreaper，也没有需要回收的孙进程，
因此不记 `own_child_subreaper=true`；可写范围是**整个**本次新 OUT（子进程要在其中写
pins/status/action-plan 与别名探针），不是"仅 fixture"。guard 只写启动器自己掌握的
预期路径与摘要，不预写"子进程已匹配"；实际匹配结果由子进程回执记录，启动器只引用其哈希。

## 校验案例

十一个案例都接到生产函数上，不另写判断。第六版全部通过：

| 案例 | 内容 | 走的生产函数 | 结果 |
|---|---|---|---|
| C0 | 用旧版路径配本轮预期摘要 | `ModuleBinding.bind()` | 导入前拒绝 |
| C1 | 落盘合同回读，比对 argv/有序输入/输出与 mapping 引用 | `emit()` 后读回 | 通过 |
| C2 | 移除一个必需 module | `validate()` 成员集合 | 拒绝 |
| C2b | 同数量替换（删 frame 全部两次出现、加未知 module） | `validate()` 未知成员门 | 拒绝 |
| C3a | 错误期望摘要 | `BindContext.read()` | 拒绝 |
| C3b | 目录别名指向同一物理文件、摘要冲突 | 同一 `BindContext.read()` | 拒绝，且未多读 |
| C4 | 回边 A→B→A | `_walk_nodes()` 状态机 | 拒绝 |
| C4b | 共享依赖 | 同一状态机 | 通过，每节点一次、两条边保留 |
| C5 | 去掉 ui/color map 的生产者、保留消费 | `check_mapping_coverage()` | 按名拒绝未覆盖输入 |
| C6 | 把 ui/color map 生产者移到 core map 之后 | `check_future_order()` | 拒绝顺序 |
| C6b | 正确顺序 | 同一函数 | 通过 |

C4/C4b 与真实闭包遍历调用**同一个** `_walk_nodes()` 状态机，只替换子节点来源；
小图例不能代表真实 loader 的存在性、摘要与边界检查。

合同完整与生成 BLOCKED 同时成立：53 个 module、core 原生 typemap 与 ui/color 原生
typemap 仍是真实缺口，`generation_permitted` 恒为 false。通过的是准备与校验，
不是生成许可。
