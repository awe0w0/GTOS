# 原生 Blink mojom_core 六 action 生成

本文件记录 `//third_party/blink/public/mojom:mojom_core` 六个原始 Mojo action 的**真实
执行结果**：667 个新增产物已产生，目标头
`third_party/blink/public/mojom/frame/frame.mojom-forward.h` 已取得。

**范围**：Content、M、CXX 编译与 GTOS 原生浏览器**均未运行**。生成成功不等于 M 的首个
报错已被越过，也不等于浏览器可以运行。

## 六个 action 与完整 owner 范围

按原始 GN 图顺序执行，不裁 owner、不裁 RSP、不裁产物：

| 顺序 | action | 阶段 | 输出 |
|---|---|---|---|
| 1 | `ui_color_mojom__parser_action` | parser | 2 module |
| 2 | `third_party_blink_public_mojom_mojom_core__parser_action` | parser | 51 module |
| 3 | `ui_color_mojom__type_mappings` | mapping | 1 map |
| 4 | `third_party_blink_public_mojom_mojom_core__type_mappings` | mapping | 1 map |
| 5 | `third_party_blink_public_mojom_mojom_core_shared__generator` | generator | 357 绑定 |
| 6 | `third_party_blink_public_mojom_mojom_core__generator` | generator | 255 绑定 |

合计 **2 + 51 + 1 + 1 + 357 + 255 = 667** 个新增产物。

这 667 个全部是**本次新增**。以下输入是**复用**，不计入 667：

| 复用项 | 数量 | 来源 |
|---|---|---|
| 旧 mojom module | 777 | 既有的 mojom_platform 生产 |
| 旧 native map | 28 | 既有的 interfaces 生产（25 新生 + 3 复用） |
| 旧生成 IDL | 2 | 既有的 blink-enums 生产 |
| 模板归档 | 1 | 既有生产 |

`mojom_core` 自身 51 个 IDL、`ui/color` 2 个 IDL，以及 121 个 owner 的 metadata 闭包
全部按原图处理。两个 parser 保留 `--check-imports`，normal generator 保留 core 原生
typemap 与默认 binding 检查，未改用 platform map 或 blink 变体。

## 目标头

`gen/third_party/blink/public/mojom/frame/frame.mojom-forward.h`

| 项 | 值 |
|---|---|
| 字节 | **2430** |
| SHA256 | `1ec9ffcb6094438482502457db0ae809b6fd94ff9294b46e75358b72f7478cd3` |

实际包含的声明（非空壳）：19 个 class 前向声明，其中接口为 `LocalFrameHost`、
`NonAssociatedLocalFrameHost`、`LocalFrame`、`LocalMainFrame`、`LocalMainFrameHost`；
struct 为 `SavableSubframe`、`GetSavableResourceLinksReply`、`FindInPageResultAXParams`、
`DownloadURLParams`、`IframeAttributes`、`LegacyTechEventCodeLocation`、
`SubframeResourceLengths`；另有 8 个 `enum class` 与 2 个 `constexpr` 常量，
并使用正确的 `THIRD_PARTY_BLINK_PUBLIC_MOJOM_FRAME_FRAME_MOJOM_FORWARD_H_` 头卫。

该 IDL 的 7 个 shared 配套全部产生：

| 文件 | 字节 |
|---|---|
| `frame.mojom-data-view.h` | 33538 |
| `frame.mojom-features.h` | 482 |
| `frame.mojom-params-data.h` | 367607 |
| `frame.mojom-send-validation.h` | 26607 |
| `frame.mojom-shared-internal.h` | 27550 |
| `frame.mojom-shared.cc` | 212958 |
| `frame.mojom-shared.h` | 48625 |

normal 侧另有 `-import-headers.h`、`-test-utils.h`、`-module`。

## 执行入口与结果

| 项 | 值 |
|---|---|
| 执行合同 | `contract-04/pins.json`，SHA `2fd298d2d36711fa8bad401b15d2e8e0a4e0a995ac08bd61a716436947ffe2dc` |
| 生产运行目录 | `run-02` |
| 六个 stage | 逐个 `returncode = 0` |
| 端到端 | 约 **680 秒** |
| 阶段解锁输入 | 1892 → 1894 → 1945 → 1946 → 1947 → 2304（逐阶段递增，证明前序产物成为后序输入） |

最终五个冻结脚本（均在仓库外）：

| 用途 | 文件 | SHA256 |
|---|---|---|
| 执行核心（合同形状/输入门/期望推导） | `execution-core-v1.py` | `659389c53208d802a3e5c974c605143c0cec3fdf2eef9ccf718629309ae9947c` |
| 当前字节资格 | `prepare-execution-contract-v5.py` | `21de17c3ae8796ef0c5924a68d22e0da9bbf0ed647494cef46b137e5d2b2c023` |
| 控制器 | `generate-core-v9.py` | `e9e2a891356b22f63b33903eb9db136a2fcc50b5bc3817a9963ce93c54ce2324` |
| worker | `mojo-core-worker-v3.py` | `983ed7f64d3bf7ec229e4445a0a3b7d82a9713f351f5a97a8f2c67bceb7afa23` |
| launcher | `launch-core-v12.py` | `a73091a1013b69bacb80d9a40e700c69d5336821fb74b7660f6f3a3b60d848fc` |

**这些脚本与产物都在 GTOS 仓库之外。** 本仓库的内容本身不能独立复现这次外部运行：
执行合同、资格回执、worker 回执与全部产物都在外部工作包中。

## 隔离与回收

入口先建立 Landlock ABI 7 规则（工作区只读、仅本次新输出目录可写、无外网、精确
Python/loader 执行白名单），再在独立 user/net 命名空间中派发子进程；launcher 真实设置
`own_child_subreaper` 并读回验证，超时后 kill 自己的进程组并 wait 回收。实测
`all_own_descendants_reaped = true`、`adopted_descendants = 0`，五个源码摘要与合同摘要
在入口结束时未变。

## 后验覆盖的三种对象

后验对三类不同对象分别处理，不能合并成一句"全部未变"：

| 对象 | 处理 |
|---|---|
| 1892 个**暂存副本** | 重读 SHA，与暂存时摘要比较 |
| 667 个**新输出** | 重读 SHA，与产生时记录比较（不用当前值覆盖旧记录） |
| 1892 个**原来源物理路径** | 比较 `st_dev`/`st_ino`/`st_mode`/`kind` 节点身份 |

前两项是内容比对，第三项是节点身份比对。当时**没有**对原来源物理路径再次做内容 SHA，
也**没有**为所有暂存副本与新输出保存起始节点基线。这两点属于当时后验的实际边界，
不能事后补成历史证明。

### 独立的来源内容补核对

生成完成后做了两次**只读**的来源内容核对，第二次发生在保存回执时。从上述冻结合同的九个 inputs 组取
`source` 与 `sha256`，物理路径去重（1935 行 → 1892 个唯一路径，0 冲突），逐个重读
当前 SHA。

| 项 | 值 |
|---|---|
| 核对条数 | **1892 / 1892** |
| 总字节 | 70,862,272 |
| 差异 | 0 |
| 耗时 | 首次核对 2.306 秒；落盘回执对应第二次核对 2.504 秒 |

这项只证明**补核对当时**内容匹配，不声称在旧运行全程监测过来源内容。回执在仓库外
`source-content-r4-01.json`。

## 下一步

要把本次 667 个产物用于真实的 M 诊断，还需先把它们纳入真实 consumer 的来源清单与
最小视图——当前 consumer 冻结入口并不知晓这些新产物。这一步尚未进行。

M 的首个报错仍只有此前记录的历史观察：`render_frame_host.h:45` 缺
`third_party/blink/public/mojom/frame/frame.mojom-forward.h`。本次生成提供了该头，
**但没有运行 M**，因此不能声称该行已越过，也不能声称下一个障碍已确认。
