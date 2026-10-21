# 生成头前沿：media_devices → capture_handle_config 的第四根

本文件记录 024 单元在生成头（generated header）前沿上已经**实测**的事实，以及仍然**未测**的边界。
它不是编译成功声明，也不是浏览器可运行声明。文中所有字节数与 SHA256 都可在
工作区 `artifacts/native-video-capture-mojom-frontier-20261021-a/` 下的回执中逐条复读。
这些脚本、回执与产物位于仓库之外，本仓库自身不能独立复现这些运行。

## 1 结论摘要

* 视频采集链路的 Mojom 生成头里，`third_party/blink/public/mojom/media/capture_handle_config`
  是 `media_devices` 的**直接**生成依赖，且它由一份早前真实生成运行产出，当前字节与产生时摘要一致。
* 按最终编译单元的有序显式 `-I` 前缀展开字面引号生成头引用，四个根分别出现 4 / 28 / 2 / 6 次，
  根内合计 40 次，**跨根去重 33 个不同的生成头**；33 项当前摘要全部匹配各自的产生时回执，
  缺权威 0、不匹配 0。
* 此集合复用了 r4、023 等历史产物；例如 geometry、native_struct 与 origin。
  来源以逐项清单为准。早先的表没有覆盖全部已接受前缀，才将部分已有依赖误判为"找不到"。
* **生成头存在不等于编译器实际读取了它。** 本文的每一条"一致"只说明字节等于某次真实动作的
  生成时摘要，不说明 clang 在预处理时到达过它。

## 2 第四根是什么，为什么是直接依赖

逻辑名：

```
gen/third_party/blink/public/mojom/media/capture_handle_config.mojom-shared-internal.h
```

注意路径里是 `mojom/media`，不是 `mojom/mediastream`。

产生它的运行是 `native-content-browser-interface-broker-20261018-a/run-04`，
其 `status.json` 为 3890435 B /
`643901a24dab1d6cc1775432f5625c24b575b57f61dadacb22647f03f9419be4`，
`outputs` 逐项记录了该文件，stage 为
`third_party_blink_public_mojom_mojom_platform_shared__generator`，`returncode 0`，
`producer_qualified true`、`complete true`。

当前文件 3368 B / `a4437226cb2e06355c22262e772f77731424455370c77201b22d05b062524f70`，
与上述产生时记录**逐字节相同**。

它是直接依赖，因为 `media_devices.mojom-shared-internal.h`
（12172 B / `f7e235e75cf7f5c01ea1575043464097c0ef017aa968aec930a1ce9f2585eedf`）
第 18 行**无条件** include 它：

```cpp
18: #include "third_party/blink/public/mojom/media/capture_handle_config.mojom-shared-internal.h"
```

不在任何 `#if` 分支内。所以"content 源码没有直接 include，因此不是编译单元依赖"这个推理不成立：
依赖是通过生成的 `media_devices` 内部头传递进来的。

## 3 三张集不要混说

| 集 | 数量 | 含义 |
|---|---|---|
| 完整生产声明集 | **931** | 四批共 12 条不同真实 edge 的**全部**声明输出（525 / 308 / 91 / 7），逐条从四份 `status.json` 的 `stage_records[*].outputs` 生成时摘要复读当前产物，0 差异 |
| 选入前缀集 | **189** | 被闭包真正引用并绑进编译前缀的成员：**162 个 `.h` + 27 个 `.cc`**，合计 189；无 module / metadata 混入 |
| 四根去重闭包 | **33** | 本文件第 5 节的字面生成头闭包；与 189 不是同一个集合，也不能互相推算 |

另有一份更早的**历史复核**记录：13 组共 **1466** 项，按各自回执复核过。它属于更早单元的证据，
不在本文件的 931 / 189 / 33 三个口径内，也不与它们相加。

931 是"生成了多少"，189 是"选进了多少"，33 是"四根递归引用了多少个不同的生成头"。
三者都不等于"编译器读了多少"。

## 4 第四批新产物的复用层级：原 seal 不改，另立一个具名裁决

这里讨论的是第四批 `singleplanar_format` 的 7 项产物，与第四根
`capture_handle_config` 是不同对象；第四根继续复用 P/run-04 已合格的生成回执，不需要此例外。

`out-r6-produce-07`（claim `produce-40a596a24fe0`）的父封印**保持原样**：
`final_code 4`、`final_ok false`、`final_reasons ["handoff_negative"]`。它没有被改写，
也不会被改写成成功。

它可以被复用的唯一路径是一个**具名、限定范围**的裁决，三个条件同时成立才生效：

1. 一份固定的审核证据文件（31900 B /
   `37f3d1e594f8fb65f3c0fd7cd60d002578d83fb2015c0977fea1a14171594747`）里，
   **唯一**一条 `conditional_specific_review_exception`，且它只点名这一个 claim、这一个
   状态摘要（`fd0e4c87d1f8f50eb4554c7ef369470b6645362bc07a2aac82bc1928a068d3bd`）、
   这一组 stage、这 7 项声明输出与这个旧 seal；
2. 该证据只允许 `handoff_negative` 这一个失败原因；deadline、cleanup、未测交接、
   debit 超限等其它原因**不在允许表内**，即使 job 与裁决一起改名也仍被拒；
3. 一次**新的只读复核**必须成功：重新读取这 7 项声明输出，逐项与生成时摘要比对，0 缺、0 变。

审核证据的路径与摘要是判定模块自己的常量，不取自 job；job 只能指出哪一份回执是例外，
不能自己写一份通过的裁决，也不能扩大其范围。

## 5 四根的字面生成头闭包（33）

| 根 | 根内出现 |
|---|---|
| `media/mojo/mojom/audio_parameters.mojom-shared-internal.h` | 4 |
| `media/mojo/mojom/media_types.mojom-shared-internal.h` | 28 |
| `mojo/public/mojom/base/time.mojom-shared-internal.h` | 2 |
| `third_party/blink/public/mojom/media/capture_handle_config.mojom-shared-internal.h` | 6 |
| 合计 / 去重 | 40 / **33** |

逐项清单（逻辑名、字节、当前 SHA256、生成期权威运行）见
`artifacts/native-video-capture-mojom-frontier-20261021-a/revision-r8/four-root-provenance-024-r8.md`。
其中三项来自本单元之外、但一直真实存在：

* `ui/gfx/geometry/mojom/geometry.mojom-shared-internal.h`（25126 B）——r4 生产；
* `mojo/public/interfaces/bindings/native_struct.mojom-shared-internal.h`（5290 B）——r4 生产；
* `url/mojom/origin.mojom-shared-internal.h`（2848 B /
  `335c76fe78ff31255a80987fb459b69ef1f3e85a8096033920c3e756b88533ad`）——023 单元生产，
  其 `out-023-produce-01/status.json` 固定摘要
  `b5878924cb912a11dd6fbbe950b0ed4561ab2741426388ada7cd05ccdd5203e0`，
  对应动作 rc0、outputs 逐项吻合。

## 6 一次真实编译单元尝试停在哪里

最终 M 参数下真实 clang 运行一次，rc1，日志唯一首错是
`content_browser_client.h:94` 缺
`payments/secure_payment_confirmation_service.mojom-forward.h`。
那是一个新的生成头家族，本单元按范围没有展开。

**该次运行只证明"编译在这里停下"，不证明 33 项闭包或 189 项前缀被逐个读取，也不证明
任何编译单元或浏览器可运行。** 本节之后的 reader 验证与例外门验证都是**新的只读验证**，
不是那次 M 运行做过的。

## 7 时间读数的窄修（r7）

父封印要判断"交接"耗时，就必须精确读到入口写下的时间。默认读取器早先经过
`ConvertFrom-Json` 再转字符串，丢掉了小数位与时区，导致短于一个整秒的交接被量成负数。
修法是**从 JSON 原文按 token 读取** `entry_utc`，再以一次 `RoundtripKind` 解析，
不经过任何文化格式渲染。

真实回执上的实测值：produce-01 为 **4.4875159 秒**，produce-07 为 **0.1360182 秒**
（入口自记 +0.136019）。旧 seal 记的 −0.7225618 只作历史对照，
**不能推出"短于 1 秒必失败"**——是否跨整秒同样影响旧读数。

这一修由 34 个案例集中验证通过，其中 7 个专测默认读取器：同秒亚秒、跨整秒、
显式 offset 等价、真实回执、真负值、debit 两侧、缺失与坏 stamp。

## 8 例外门的验证（r8）

r7 的例外门有一个实质缺陷：它把 job 给的 review 摘要与 job 自己期望的摘要相比，
把待审 seal 里的失败原因复制出来再与同一个 seal 相比，并且允许 job 直接提供一份裁决。
r8 改为从固定审核证据**读取内容**并据此判定，且 job 提供裁决即被拒绝。

集中受控验证（一次只读 check）的实测结果：

| 项 | 结果 |
|---|---|
| 三份正常生产回执 | 均判 `historical_successful_producer` |
| 唯一例外回执（带裁决） | 判 `specific_review_exception` |
| 唯一例外回执（无裁决） | 判 `none`（拒绝） |
| 审核证据缺失 | 拒绝（`review_authority_missing`） |
| 审核证据改字节 | 拒绝（`review_authority_changed`） |
| job 自带裁决 / 自带 revalidation | 在加载层被拒（`adjudication_declared_by_job`） |
| job 与裁决一起换 claim/摘要 | 拒绝（超出真实授权） |
| seal、job 期望与裁决一起改名 deadline / cleanup | 拒绝（不在独立允许原因表内） |
| 正控失败而负控全拒 | 汇总为失败 |

该轮共 21 项控制，其中 20 项按预期拒绝/通过，最终 check 仍为 **exit 150 / ok=false**。
第 21 项"必需正控缺失"的自检在必需集已经构造后删除条目，没有驱动
“按独立 authority 构造必需集、补记缺项、再汇总”的完整路径。
该负例**未获验证**；不能把实际 4/4 正控通过与 20/21 控制通过写成整批成功。

## 9 未测边界

1. 未做 C++ 编译成功、未链接、未运行 content_shell 或浏览器。生成头前沿的结论止于"字节一致"。
2. 33 项闭包与 189 项前缀都不等于 clang 实际读取集合。
3. 未做 GN 再生，未新增 ABI、宏或 SDK 面。
4. Windows 侧终止进程不构成 Linux 回收证据。
5. 第 8 节第 21 项自检有构造缺陷，未通过。
