# 原生 Content：payment_confirmation 常规生成头前沿

024-r6 的真实 Content `-M` 停在
`third_party/blink/public/mojom/payments/secure_payment_confirmation_service.mojom-forward.h`
缺失，位置是 `content/public/browser/content_browser_client.h:94`。该头不属于此前任何一轮
的 shared generator 家族，而属于一条**常规**（normal）mojom generator 边。025 只做一件事：
用同一台 production 生成器把这条边的完整声明跑出来，再取一次统一 M，看首错是否越过该 include。

结果：125 项声明产物全部产生（rc 0），M 的 argv 由 332 变为 333，第 94 行的 payments 头**已解析**，
首错前移到同一文件的第 95 行引入的另一个家族。M = **RAN_RC1**：clang 真实启动一次、rc1、
120.180 秒；这是诊断流程有效，不是 CXX 编译、链接或浏览器运行成功。

## 1. 这一轮的边界

- 图未变：`toolchain.ninja` 45456305 B，
  SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`，
  边在第 20229 行。
- 目标 ACTION：`//third_party/blink/public/mojom:android_mojo_bindings__generator(//build/toolchain/gtos:clang_x86)`，
  规则名 `__third_party_blink_public_mojom_android_mojo_bindings__generator___build_toolchain_gtos_clang_x86__rule`。
- **规则名里的 `android` 不是 Android 运行授权**。编译 triple 仍是 `i686-unknown-none-elf`，
  `__GTOS__`、SDK、宏集合与图一律不动；没有新增 `__ANDROID__`、`USE_AURA`、`USE_OZONE`，
  没有新增 `-D`，没有 `-MG`。本轮只新增一个 `-I`。
- 只新增 1 个 ACTION owner、1 次 ACTION 启动、1 次 clang 启动。

## 2. 声明先冻结，再执行

输入表完全由那条边的**自身文本**导出，没有从任何回执抄写：

| 项 | 数量 |
|---|---:|
| 声明产物 | 125（25 个 module × 5：`-forward.h`、`-import-headers.h`、`-test-utils.h`、`.h`、`.cc`）|
| 边输入的 `gen/` 唯一项 | 285 |
| 加生成器模板归档 | 286 |
| 其中带产生时摘要（已接受回执） | 234 |
| 仅本次观测（GN 写的 51 个 `.build_metadata` + 生成器 rsp） | 52 |

286 项中 234 项按 `run-02`、`run-04`、`mojo-generate-01` 三家已接受回执记录的
产生时摘要逐字节复核；其余 52 项没有任何 mojom ACTION 产生过它们，标注为
`current_observation_only`，不冒充历史资格。

prepare 通过：286 项全部定位、170 个 runtime 对象从声明的 source view 读到，
closure 621695 B / `3c8793f67b754b58671063018e110e1d3a3aa017260a1693605d4d04bc0b1fd8`。这一步不启动任何 ACTION。

## 3. 真实 produce

一条边、一次启动、rc 0、11.621 秒，125 项声明产物全部写出，含
`gen/third_party/blink/public/mojom/payments/secure_payment_confirmation_service.mojom-forward.h`
（1255 B，SHA256 `c7b51906aeb3d4326f9ebd8143dceafadc7e2fc8128c25bfee630fff3670dab6`）。
声明是完整性闸门：少一项即拒绝，不是"取前缀"。

## 4. 统一 M 的实测变化

| 项 | r6 M（基线） | 025 M |
|---|---:|---:|
| argv 元素 | 332 | **333** |
| 插入位置 | — | 127（r6 prefix 之后、`-I../source` 之前）|
| 新 prefix 成员 | 189 | 125（本轮）|
| 旧 prefix 组 | 13 组 / 1466 | **14 组 / 1655** |
| 成员解析 | 189 | 125 |
| 差异下标（基线坐标） | [213, 329] | **[214, 330]** |

差异只有两个值：`-fcrash-diagnostics-dir=` 与 `-MF` 的目标目录。移除新插入的 `-I` 后，
得到与基线等长的 332 项数组；其中 330 项相同，另两项仅输出值改变。旧 14 组每组都按**自己的**回执复核
（`012` 4、`013` 250、`014` 9、`015` 5、`016` 170、`018` 5、`019` 25、`020` 5、
`021` 190、`022` 60、`023` 708、`r3` 7、`r4` 28、`r6` 189），合计 1655。

## 5. 首错前移，但只前移一行

- r6：`content_browser_client.h:94` → `payments/secure_payment_confirmation_service.mojom-forward.h` 找不到。
- 025：第 94 行通过，第 95 行 → `third_party/blink/public/mojom/worker/shared_worker_info.mojom.h`
  → `frame/policy_container.mojom-shared-internal.h:17` →
  `services/network/public/mojom/integrity_policy.mojom-shared-internal.h` 找不到。

也就是说：本轮补齐的正是 r6 停住的那一个 include，M 因此向前推进；新的前沿是
`services/network/public/mojom` 家族，不是 payments 家族的延续。按任务书"首个有效新家族诊断即停"，
本轮到此为止，不再补第二个家族。

## 6. 没有做的事

未跑 CXX 编译、链接、`content_shell`/browser；未跑 network service；未重新生成 GN；
未改 ABI、宏或 SDK；未跑 kernel/ISO/guest/display；未推送。

## 7. 控制收口与证据定位

025 的只读 check 已实际通过：4/4 必需正控与 22/22 拒绝控制通过，
缺一个真实生产者及其 judgement 时，固定 authority 仍要求四项；同一生产构造函数补出
具名 missing，汇总为失败。全空输入与“正控失败、负控全拒”也被拒绝。
这是 025 新证据，不把 024-r8 当时的 exit 150 改写为成功。

以下路径均相对工作区中的
`artifacts/native-video-capture-mojom-frontier-20261021-a/revision-r5/continuation-025/`，
不是相对本 Git 仓库。脚本、回执和产物在仓库之外，本仓库自身不能独立复现这些运行。

| 对象 | 路径 | SHA256 |
|---|---|---|
| 缺项负例与正控 | `out-025-check-01/producer-qualification-025.json` | `2eb3a27a7fcefc851d478038f1f9a8e6a3a24f544c1457e757d68a79a7e49247` |
| 完整生产回执 | `out-025-produce-01/status.json` | `457fa4c363beacb85cffba1b85d8357e5c80ec6c1e3a926527859e198f72c23e` |
| M 回执 | `out-025-M-01/status.json` | `34fc33b01f080b1b121aa55de85757a737ce6f6b4d827d11077e2ddac3830194` |
| 原始 M 日志 | `out-025-M-01/delegate-M.log` | `b333e9f117228341231edc7686294713d24babc10982e61142b9214b2efa772c` |

三次失败 prepare 均启动了受控入口及 worker，没有启动 GN ACTION；它们已分别计费。
产物完整生成与前缀解析不代表 125 项都被 clang 读取；M 仍为 RAN_RC1。
