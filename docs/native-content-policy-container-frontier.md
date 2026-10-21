# 原生 Content：policy_container 的七个 network 直接 include 前沿

025 的真实 Content `-M` 停在
`gen/third_party/blink/public/mojom/frame/policy_container.mojom-shared-internal.h:17`，
缺失的是 `services/network/public/mojom/integrity_policy.mojom-shared-internal.h`。
该头是 `policy_container` 的**直接** include，同组还有六个兄弟，都是
`services/network/public/mojom` 的 shared-internal 头。026 只做一件事：把
`policy_container.mojom-shared-internal.h` 直接 include 的这七个 network 根、以及它们
**必要的递归生成头闭包**一次冻结、一次生产、一次统一 M，看首错是否越过这七行。

结果：八个 ACTION、399 项声明产物全部产生（rc 0），argv 由 333 变为 **334**，
第 14–20 行的七个根**全部解析**，首错前移到同一文件的第 21 行引入的
`third_party/blink/public/mojom/tokens/tokens.mojom-shared-internal.h`。
M = **RAN_RC1**：clang 真实启动一次、rc1、119.356 秒；这是诊断流程有效，
不是 CXX 编译、链接或浏览器运行成功。

## 1. 这一轮的边界

- 图未变：`toolchain.ninja` 45456305 B，
  SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
- 七个根（任务书原文顺序）：`connection_allowlist`、`content_security_policy`、
  `cross_origin_embedder_policy`、`integrity_policy`、`ip_address_space`、
  `referrer_policy`、`web_sandbox_flags`。
- 编译 triple 仍是 `i686-unknown-none-elf`，`__GTOS__`、SDK、宏集合与图一律不动；
  没有新增 `-D`、没有 `-MG`、没有新增 ABI 或宏。本轮只新增一个 `-I`。
- **不构建整个 network**：只运行携带这七个根及其递归闭包的八条原始 ACTION，
  没有执行整个 `//services/network` 目标的构建，也没有运行任何 CXX。
- 只新增 8 个 ACTION owner、8 次 ACTION 启动、1 次 clang 启动。

## 2. 八条边是实测出来的，不是推定的

七个根不都由"自己名字"的边产生。本轮必要生产集包含七条单 module 的 shared
generator 边和一条批量边。其中三个根（`cross_origin_embedder_policy`、`ip_address_space`、
`referrer_policy`）以及递归依赖 `fetch_api`，由 **350 项产物 / 50 个 module 词干**的
`url_loader_base` 批量边产生；在固定图中，该边是这四个 module 的唯一生产者。
下表仅列七个根；其余三条单 module 边处理递归依赖：

| 根 | 生产者边 | 边所在行 | 声明产物 |
|---|---|---:|---:|
| `connection_allowlist` | `…:mojom_connection_allowlist_shared__generator` | 15456 | 7 |
| `content_security_policy` | `…:mojom_content_security_policy_shared__generator` | 15545 | 7 |
| `cross_origin_embedder_policy` | `…:url_loader_base_shared__generator` | 17503 | 350 |
| `integrity_policy` | `…:mojom_integrity_policy_shared__generator` | 16067 | 7 |
| `ip_address_space` | `…:url_loader_base_shared__generator` | 17503 | 350 |
| `referrer_policy` | `…:url_loader_base_shared__generator` | 17503 | 350 |
| `web_sandbox_flags` | `…:mojom_web_sandbox_flags_shared__generator` | 17414 | 7 |

八个 ACTION 的产物路径互不重叠：七条单 module 边的 49 项产物与
`url_loader_base` 的 350 项产物交集为 **0**。这一点是必须实测的，
否则"补一个根"会悄悄覆盖另一条边已经声明的文件。

`url_loader_base` 的实测形状值得单独记下：它声明 **350 项产物 / 50 个 module 词干**，
却 **staged 149 个 `.mojom-module`** 输入——149 个输入词干中 99 个在本边没有产物，
50 个有产物，且没有"有产物而无输入"的词干。也就是说这条边读的 module 比它渲染的多，
按 module 数而不是按产物数估算它的输入会错。

## 3. 递归生成头闭包：14 个头，三层来源

从固定 source view 静态算出七个根的**传递 IDL import 闭包**共 14 个 module。
本节的“14 个头”特指每个 module 对应的一个 `.mojom-shared-internal.h`，
不是全部生成头后缀的递归闭包，也不表示它们都需要本轮新产出：

- 七个根本身；
- `mojo/public/mojom/base/unguessable_token`、`url/mojom/url`、`url/mojom/origin`；
- `services/network/public/mojom` 的 `integrity_algorithm`、`integrity_metadata`、
  `source_location`（经 `content_security_policy` 传递）、`fetch_api`
  （经 `cross_origin_embedder_policy` 传递）。

这 14 个头按来源分成三层，且三层是**实测的划分**而不是假设：

| 层 | 数量 | 头 |
|---|---:|---|
| 本轮 produce 渲染 | 11 | 七个根 + `integrity_algorithm`、`integrity_metadata`、`source_location`、`fetch_api` |
| 复用已接受回执 | 1 | `url/mojom/url`（`mojo-generate-01` 记录的生成时摘要 2524 B / `221b237a…`）|
| 已由 023 前缀提供 | 2 | `unguessable_token`、`url/mojom/origin` |

第三层是在第一次 content 入口尝试时查出的：该次在 plan gate 就被拒绝
（`old_prefixes: the new prefix collides with an earlier group on 2 header(s)`，
入口判定码 146，**clang 未启动**）。本轮选择门按 logical 拒绝重复提供；
这次命中并不证明有不同字节冲突。因此两个头移出新前缀，
改为对 023 组的正向断言：该组自己的 M 回执记录 `unguessable_token` 2861 B /
`5cdf4507…`、`origin` 2848 B / `335c76fe…`，且盘上字节与记录一致。
新前缀因此是 **12 项**，而不是 14 项。

## 4. 声明先冻结，再执行

输入表完全由八条边的**自身文本**导出，没有从任何回执抄写：

| 项 | 数量 |
|---|---:|
| 声明产物（7×7 + 350） | 399 |
| 八条边 `gen/` 输入的并集 | 190（149 `.mojom-module` + 33 `.build_metadata` + 8 `.rsp`）|
| 加生成器模板归档 | 191 |
| 其中带产生时摘要（已接受回执） | 150 |
| 仅本次观测（GN 写的 33 个 `.build_metadata` + 8 个 rsp） | 41 |

191 项中 150 项按 `run-02`、`generate-04`、`mojo-generate-01` 三家已接受回执记录的
产生时摘要逐字节复核（106 / 40 / 4）；其余 41 项没有任何 mojom ACTION 产生过它们，
标注为 `current_observation_only`，不冒充历史资格。

prepare 通过：191 项全部定位、170 个 runtime 对象从声明的 source view 读到，
closure 603556 B / `3c3252ef7c7bab7a5757f452af0221e3b7a3bd9d5aa2650f519128fe94daaee8`。
这一步不启动任何 ACTION。

## 5. 真实 produce

八个 ACTION、八次启动、全部 rc 0，worker 141.227 秒，399 项声明产物全部写出。
声明是完整性闸门：少一项即拒绝，不是"取前缀"。

| 边 | 秒 | 产物 |
|---|---:|---:|
| `connection_allowlist` | 3.942 | 7 |
| `content_security_policy` | 3.943 | 7 |
| `integrity_algorithm` | 3.892 | 7 |
| `integrity_metadata` | 3.993 | 7 |
| `integrity_policy` | 3.993 | 7 |
| `source_location` | 3.942 | 7 |
| `web_sandbox_flags` | 4.380 | 7 |
| `url_loader_base` | 21.558 | 350 |

七个根与另外四个闭包头都写成了真实字节，例如
`cross_origin_embedder_policy.mojom-shared-internal.h` 3935 B /
`573303c1fd844daa6832264e286e204594f6e3b0e5f6774d467a43ed32a7cd3c`；
`integrity_policy.mojom-shared-internal.h` 4363 B /
`b37fa8533ade703ba32e28955a70c95db8e466deebe227bfd61783665065a076`。

## 6. 统一 M 的实测变化

| 项 | 025 M（基线） | 026 M |
|---|---:|---:|
| argv 元素 | 333 | **334** |
| 插入位置 | — | 128（025 prefix 之后、`-I../source` 之前）|
| 新 prefix 成员 | 125 | 12（本轮）|
| 旧 prefix 组 | 14 组 / 1655 | **15 组 / 1780** |
| 成员解析 | 125 | 12 |
| 差异下标（基线坐标） | [214, 330] | **[215, 331]** |

差异只有两个值：`-fcrash-diagnostics-dir=` 与 `-MF` 的目标目录。移除新插入的 `-I` 后，
得到与基线等长的 333 项数组；其中 331 项相同，另两项仅输出值改变。
旧 15 组每组都按**自己的**回执复核（`012` 4、`013` 250、`014` 9、`015` 5、`016` 170、
`018` 5、`019` 25、`020` 5、`021` 190、`022` 60、`023` 708、`r3` 7、`r4` 28、
`r6` 189、`025` 125），合计 1780。

## 7. 首错越过七行，落到新家族

- 025：`frame/policy_container.mojom-shared-internal.h:17` →
  `services/network/public/mojom/integrity_policy.mojom-shared-internal.h` 找不到。
- 026：第 14–20 行**全部通过**，第 21 行 →
  `third_party/blink/public/mojom/tokens/tokens.mojom-shared-internal.h` 找不到。

也就是说本轮补齐的正是 `policy_container` 直接 include 的那七个 network 根，
第 13 行的 `unguessable_token` 已由 023 前缀提供，第 21 行的 `tokens` 不在本轮七根子图内。
本次实际新首错只有 `third_party/blink/public/mojom/tokens/tokens.mojom-shared-internal.h`。
第 23 行的 `native_struct` 尚未由这次 clang 执行走到，不能把它写成新缺失点，
也不能据此声称它不存在。按任务书"第一份有效 M 越出冻结子图即停"，
本轮到此为止，不再追下一族。

## 8. 没有做的事

未跑 CXX 编译、链接、`content_shell`/browser；未跑 network service；未构建整个
`services/network`；未重新生成 GN；未改 ABI、宏或 SDK；未跑 kernel/ISO/guest/display；
未推送。原生 Chromium 从未运行；本次 M 也不构成浏览器可运行的证据。

## 9. 证据定位

以下路径均相对工作区中的
`artifacts/native-video-capture-mojom-frontier-20261021-a/revision-r5/continuation-026/`，
不是相对本 Git 仓库。脚本、回执和产物在仓库之外，本仓库自身不能独立复现这些运行。

| 对象 | 路径 | SHA256 |
|---|---|---|
| 冻结输入表 | `inputs-026-policy-container-v1.json` | `1583963fa3a650f64516338145839eb6ce019424c7a760467d37319e945172fa` |
| 真实闭包 | `out-026-prepare-03/closure-024.json` | `3c3252ef7c7bab7a5757f452af0221e3b7a3bd9d5aa2650f519128fe94daaee8` |
| 完整生产回执 | `out-026-produce-01/status.json` | `b406347c67307ec10810092b344695e21e703ae773f479385b918b39345dd77f` |
| M 回执 | `out-026-M-02/status.json` | `d464f445e401622c7b1b2951db529d2492f5dec98c1e364943bf50bd062df442` |
| 原始 M 日志 | `out-026-M-02/delegate-M.log` | `0cd19e7336cc0678497303ae4e0b562a9c4dee86c05c82ca3c496e949fc4dbf3` |
| M 计划 | `plan-026-M-v2.py` | `e2c20abd6bffa63a56773e8fefc82034d59a95aecfb4684fef71dcedee2fc512` |

第一次统一 M（`out-026-M-01`）在 plan gate 被拒绝、未启动 clang，
该失败回执保留在原处、不改写。产物完整生成与前缀解析不代表 12 项都被 clang 读取；
M 仍为 RAN_RC1。
