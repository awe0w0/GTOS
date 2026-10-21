# 原生 Content：tokens 的 shared-internal 前沿

026 的真实 Content `-M` 停在
`gen/third_party/blink/public/mojom/frame/policy_container.mojom-shared-internal.h:21`，
缺失的是 `third_party/blink/public/mojom/tokens/tokens.mojom-shared-internal.h`。
027 只做一件事：用这条边**自己的完整声明**把这一个头渲染出来，作为一个前缀成员追加到
026 的 334 项 argv 上，看首错是否越出 tokens 子图。

结果：一个 ACTION、七项声明产物全部产生（rc 0），argv 由 334 变为 **335**，
该头在真实 include 根下解析，首错前移到
`gen/third_party/blink/public/mojom/worker/worker_options.mojom-shared-internal.h:14`
引入的 `third_party/blink/public/mojom/script/script_type.mojom-shared-internal.h`。
M = **RAN_RC1**：clang 真实启动一次、rc1、118.466 秒；这是诊断流程有效，
不是 CXX 编译、链接或浏览器运行成功。

## 1. 这一轮的边界

- 图未变：`toolchain.ninja` 45456305 B，
  SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
- 边只有一条：rule 第 22171 行、build 第 22176 行，
  `third_party_blink_public_mojom_tokens_tokens_shared__generator`。
- 编译 triple 仍是 `i686-unknown-none-elf`，`__GTOS__`、SDK、宏集合与图一律不动；
  没有新增 `-D`、没有 `-MG`、没有新增 ABI 或宏。本轮只新增一个 `-I`。
- 只新增 1 个 ACTION owner、1 次 ACTION 启动、1 次 clang 启动。
- 首错一旦越出 tokens 子图即停，不追下一族。

## 2. 这条边声明七项产物，但只有一项是前缀成员

共享生成器对一个 module 渲染七项：

| 产物 | 字节 | 本轮摘要 | 进前缀 |
|---|---:|---|---|
| `tokens.mojom-data-view.h` | 38543 | `74f4c50f…` | 否 |
| `tokens.mojom-features.h` | 490 | `bbccbafc…` | 否 |
| `tokens.mojom-params-data.h` | 829 | `8ec44e55…` | 否 |
| `tokens.mojom-send-validation.h` | 51302 | `a2f68846…` | 否 |
| **`tokens.mojom-shared-internal.h`** | **48308** | **`2e755c8ae1a005aa4c209e9ea543cc87e2f74cf04daede15ee0847aa04db2085`** | **是** |
| `tokens.mojom-shared.cc` | 31460 | `680514f7…` | 否 |
| `tokens.mojom-shared.h` | 71102 | `78123960…` | 否 |

七项全部产生并进回执。本轮冻结的选择集只提供当前缺失的
`tokens.mojom-shared-internal.h`，其余六项保留为配套产物。
这不表示其余文件不能被其他 include 使用，也不表示将它们选入一定会产生冲突；
重复与冲突由实际 logical 检查判定，本轮选择集的检查结果为零命中。

## 3. 44 项输入，40 项带真实生成时摘要

图的 149 条依赖边归并为 44 项 staged 输入：43 个 gen 文件加模板归档。43 个里：

- **40 个 `*-module`**，且每一个都有已接受阶段记录的生成时摘要：
  - 39 个 `mojo/public/mojom/base/*.mojom-module` → `generate-04`（563 项产物）；
  - `tokens.mojom-module` → `run-02`（934 项产物，17259 B / `0c9fe39d…`）。
- 2 个 `.build_metadata`（`base.build_metadata`、`tokens.build_metadata`）；
- 1 个生成器 `.rsp`。

**这条边并非只带自己的 module**：40 个 module 里 39 个来自另一份回执。
40 项全部从盘上重新读出、与记录逐字节比对，全部一致；在多个根同时存在的 41 项输入，
各副本两两同字节。

43 个 gen 输入中的另外 3 项（2 个 GN 写的 `.build_metadata` + 1 个 rsp）
没有 mojom ACTION 生产者，标注为 `current_observation_only`，不冒充历史资格。

第 44 项是模板归档 `cpp_templates.zip`（574664 B），摘要为
`4763b1927c982adf683e7517d3fa650b9eea4f2629f4914e99fc37cbb070fe39`。
027 原始 closure 将它也标为 `current_observation_only`；审核时独立补核，
其摘要与已接受 U/run-02 的 `reused_qualified_outputs` 记录相等。
因此 40 是本轮有复用记录的 module 数，不是全部历史来源匹配项数；
这项补核不改写为 027 当时已按该历史记录放行。

## 4. IDL 闭包与生成头闭包分开计数

`tokens.mojom`（4402 B / `bebd3b3d…`）只 import 一个文件：
`mojo/public/mojom/base/unguessable_token.mojom`，而后者没有 import。

任务书提醒不要未加核查就把 `unguessable_token` / `native_struct` 称作缺失。两者都查了，
**两者都已有现成渲染**：

| 头 | 归属组 | 该组回执记录的摘要 | 盘上 |
|---|---|---|---|
| `unguessable_token.mojom-shared-internal.h` | **023**（2861 B） | `5cdf4507…` | 一致 |
| `native_struct.mojom-shared-internal.h` | **r4**（5290 B） | `857f7398…` | 一致 |

IDL import 闭包是两个 module：`tokens` 与 `unguessable_token`。
`tokens.mojom-shared-internal.h` 还直接 include `native_struct.mojom-shared-internal.h`，
所以这里选定的生成头集合是 **3 = 1 本轮渲染 + 2 已由早前组提供**。
`native_struct` 不来自这个 IDL import 图，也不是本轮缺失前沿；r4 的 M 回执及当前字节均有对应记录。
“0 复用”只表示没有旧头被复制进新前缀，不表示本轮没有复用既有头和 module。

两者都没有被重新提供，而是在**它们各自所在的组里**按该组回执记录的摘要做正向断言。
实际头解析仍遵循 `-I` 顺序；这些断言验证了既定顺序下的预期来源及其字节，
并避免新前缀再次提供同名文件。

## 5. 统一 M 的实测变化

| 项 | 026 M（基线） | 027 M |
|---|---:|---:|
| argv 元素 | 334 | **335** |
| 插入位置 | — | 129（026 prefix 之后、`-I../source` 之前）|
| 新 prefix 成员 | 12 | 1（本轮）|
| 旧 prefix 组 | 15 组 / 1780 | **16 组 / 1792** |
| 成员解析 | 12 | 1 |
| 差异下标（基线坐标） | [215, 331] | **[216, 332]** |

移除新插入的 `-I` 后得到与基线等长的 334 项数组；其中 332 项相同，
另两项仅 `-fcrash-diagnostics-dir=` 与 `-MF` 的输出值不同。
插入导致的下标移动与这两项值变化是不同现象，不能说仅删除 `-I` 就字节级还原基线。
旧 16 组每组都按**自己的**回执复核（`012` 4、`013` 250、`014` 9、`015` 5、
`016` 170、`018` 5、`019` 25、`020` 5、`021` 190、`022` 60、`023` 708、`r3` 7、
`r4` 28、`r6` 189、`025` 125、`026` 12），合计 1792。

逐逻辑的碰撞检查对着每个早前组**自己的**拼写表做，结果 0 命中。负向控制全部触发：
抬高 include 根被拒、篡改 flag 被检出、篡改 define 被检出、加 `-MG` 被检出。

## 6. 首错越出 tokens 子图

- 026：`policy_container.mojom-shared-internal.h:21` →
  `third_party/blink/public/mojom/tokens/tokens.mojom-shared-internal.h` 找不到。
- 027：该行**通过**，首错变为
  `worker/worker_options.mojom-shared-internal.h:14` →
  `third_party/blink/public/mojom/script/script_type.mojom-shared-internal.h` 找不到。

tokens 这一族已在前沿之后。新的前沿是
`third_party/blink/public/mojom/script`（经 `worker` 到达），与本轮冻结的 tokens
子图不是同一条链。按任务书"第一份有效 M 越出冻结子图即停"，本轮到此为止。

## 7. 没有做的事

未跑 CXX 编译、链接、`content_shell`/browser；未构建整个 Blink platform；
未重新生成 GN；未改 ABI、宏或 SDK；未跑 kernel/ISO/guest/display；未推送；
没有删改或改名任何旧目录、旧回执、旧 manifest；账本只由外层父进程追加。
原生 Chromium 从未运行，本轮 M 不构成浏览器可运行的证据。

## 8. 证据定位

以下路径均相对工作区中的
`artifacts/native-video-capture-mojom-frontier-20261021-a/revision-r5/continuation-027/`，
不是相对本 Git 仓库。脚本、回执和产物在仓库之外，本仓库自身不能独立复现这些运行。

| 对象 | 路径 | SHA256 |
|---|---|---|
| 冻结输入表 | `inputs-027-tokens-v1.json` | `42612db3d808eb30568a64e25b40711cb8dc32e250f50357bf5509ad3fb7eab1` |
| 真实闭包 | `out-027-prepare-02/closure-024.json` | `8f87635c3c9c2e1d0fc9d02e899dd8e020b878a75f862c8e960a2b7f93f0582e` |
| 完整生产回执 | `out-027-produce-01/status.json` | `af69caff5c32a86b7ad852ec0a2960e0d3e014468d03ddf2b93a3549b445eda3` |
| M 回执 | `out-027-M-01/status.json` | `814cec5ef5091d02b70eee9c51bb4f2cdfa81ff873597c0972e16b13d2b593ae` |
| 原始 M 日志 | `out-027-M-01/delegate-M.log` | `adf349fb3e890a4ff764f9a9fe0cfc515e182e29b16e34b3f83f913b4645a72f` |
| M 计划 | `plan-027-M-v1.py` | `8266dacc042d8a0d25de8349a1fadcedc9a22851527e8be5c1b657613a3dd7ad` |
| 声明/选择/碰撞/来源/闭包表 | `tables-027.json` | `134e0bbc3bcc23013db18223d9c3b6e77dfb6ee1bf222d7493eb8148d3ddf7d6` |

单靠产物完整生成与前缀解析不能证明 clang 读取过它；本轮还依据同一固定 include 链
越过 `policy_container:21`、到达随后 `worker_options` 的原始诊断来确认缺失点已越过。
不声称另外六项产物也被 clang 读取；M 仍为 RAN_RC1。
