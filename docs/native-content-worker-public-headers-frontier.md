# 原生 Content：worker 的公开生成头闭包

028 的真实 Content `-M` 停在
`gen/third_party/blink/public/mojom/frame/policy_container.mojom-shared.h:28`，
缺失的是 `services/network/public/mojom/integrity_policy.mojom-shared.h`。
该头在 026 的真实产物里已经存在（11402 B，产生时摘要
`1f4870ae240e6cf5098c20729f291791a7238e68afecac2db8f9b5a908f022b7`），
物理字节与当时 stage 记录相等。027 / 028 的完整产物里也有 `tokens` 与
`script_type` 的公开 shared / data-view 配套。

029 因此**不渲染任何东西**：它先把这条包含链上真正需要的生成头闭包冻结下来，
再把闭包里"已产出但未选入"的那几项从各自已接受回执里取出来，放入一个新前缀目录，
追加一个 `-I` 到 028 的 336 项 argv 上，看首错是否越出这个闭包。

结果：**0 个 ACTION**、argv 由 336 变为 **337**、六个头在真实 include 根下解析，
首错前移到

```
gen/third_party/blink/public/mojom/frame/policy_container.mojom.h:32:10: fatal error:
    'services/network/public/mojom/integrity_policy.mojom.h' file not found
```

M = **RAN_RC1**：clang 真实启动一次、rc1、123.927 秒。首错已越出冻结闭包，
因此在这里停链，不追下一族。rc1 只说明诊断流程有效，不是 CXX 编译、链接或
浏览器运行成功。

## 1. 这一轮的边界

- 图未变：`toolchain.ninja` 45456305 B，
  SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
- 编译 triple 仍是 `i686-unknown-none-elf`，SDK、宏集合与图一律不动；
  没有新增 `-D`、没有 `-MG`、没有新增 ABI 或宏。本轮只新增一个 `-I`。
- 0 个新 ACTION owner、0 次 ACTION 启动、1 次 clang 启动。
- 受测单元未变：`content/shell/app/shell_main_delegate.cc`。
- 首错一旦越出冻结闭包即停，不追下一族。

## 2. 闭包是六十一项，不是"六项"

把根提升到当前实际包含链上的
`gen/third_party/blink/public/mojom/worker/shared_worker_info.mojom-shared.h`，
按 028 真 argv 的 74 个搜索目录（19 个前缀 overlay → `../source` → `gen`）静态展开
生成头的 `#include` 图，普通源码头单列、不展开，得到 **61** 个生成头：

| 项 | 值 |
|---|---:|
| 生成头 | 61 |
| 冻结 `-I` 序已能解析 | 55 |
| 冻结 `-I` 序解析不到 | 6 |
| 单列普通源码头 | 16 |

55 个已供项按供给层分布（索引为 028 真 argv 中的 `-I` 位置）：

| 索引 | 供给层 | 数量 |
|---:|---|---:|
| 123 | 023 的 M overlay | 6 |
| 125 | r4 的 M overlay | 1 |
| 128 | 026 的 M overlay | 12 |
| 129 | 027 的 M overlay | 1 |
| 130 | 028 的 M overlay | 1 |
| 132 | 基础 `-Igen` | 34 |

闭包里 `*.mojom.h`（完整绑定头）形状的成员数为 **0**：这 61 项只含
`-shared.h`、`-data-view.h`、`-shared-internal.h` 三种形状。这一点在 §5 会用到。

闭包不靠 IDL import 集合替代，也不靠"完整边输出数 × 常数"猜：每个成员都是
从盘上按真实 `-I` 序打开、当场重算字节与摘要得到的。
55 个已供项中，21 项还与各自旧组产生时摘要核对；基础 `-Igen` 的 34 项
只具有本轮冻结记录与当前字节，没有在本轮补成历史生成资格。
1794 个旧组成员与这 55 项是不同口径，不能直接相加当作不重复的文件集合。

## 3. 六个未供项，各自来自一份已接受回执

| 头 | 字节 | 产生时摘要 | 回执 |
|---|---:|---|---|
| `services/network/public/mojom/integrity_policy.mojom-shared.h` | 11402 | `1f4870ae…` | 026 |
| `services/network/public/mojom/integrity_policy.mojom-data-view.h` | 4485 | `fcb0e1a3…` | 026 |
| `third_party/blink/public/mojom/tokens/tokens.mojom-shared.h` | 71102 | `781239607…` | 027 |
| `third_party/blink/public/mojom/tokens/tokens.mojom-data-view.h` | 38543 | `74f4c50f…` | 027 |
| `third_party/blink/public/mojom/script/script_type.mojom-shared.h` | 3080 | `04f9190a…` | 028 |
| `third_party/blink/public/mojom/script/script_type.mojom-data-view.h` | 1624 | `beb01410…` | 028 |

三份回执在取用前逐份判定：claim 身份、每个 stage 的 action（rc 恰为 0、completed、
未超时 / 未杀 / 未异常）、每个 stage 自己的 post-check（action / actual_set /
cleanup / deadline、剩余进程 0、sweep 已测）、stage 集与声明产物数、ledger seal。
026 为 8 个 stage / 399 项，027 为 1 个 stage / 7 项，028 为 1 个 stage / 14 项，
合计 420 项，全部判为历史成功，未使用任何例外。

每个被取用的头都从**该回执自己命名的 tree** 复制，复制后按回执记录的产生时摘要与
字节重核。对不上就拒绝后续编译器释放；复制发生在核对之前，失败 OUT 可以保留已写副本。

## 4. 420 项里为什么只取 6 项

因为闭包只有这 6 项解析不到。026 的 `integrity_policy` 那条边声明了 7 项产物，
本轮新前缀只取 2 项，另外 5 项是 `-features.h`、`-params-data.h`、`-send-validation.h`、
`-shared-internal.h`、`-shared.cc`。未选入有两种情况：其中 `-shared-internal.h`
已经在闭包内，由 026 自己的 M overlay 供给；另外四项不在这次冻结闭包里。
不能把“本轮不再供给”写成“闭包不经过它”，也不能推断它们必然与前缀冲突。

选择集恰好等于闭包里解析不到的六项，其余 414 项没有复制到新目录。
本轮实际选择集与旧组逐 logical 碰撞检查为零命中；没有测量把全部 420 项
选入后的解析结果，也不据此推断其必然冲突。

## 5. 新的首错属于另一个生成器家族

三点，都可复核：

1. 报错文件 `policy_container.mojom.h` **不在** 61 个成员里——闭包内 `*.mojom.h`
   形状成员数为 0。
2. 缺失的 `integrity_policy.mojom.h` 也不在闭包里，而且不在任何 `-I` 目录中。
   基础 `-Igen` 的真实目录里，该模块只有 `integrity_policy.mojom-module`，没有
   `.mojom.h`；而同目录的 `connection_allowlist`、`content_security_policy`、
   `integrity_algorithm`、`integrity_metadata`、`source_location`、
   `web_sandbox_flags` 都有 `.mojom.h`。这些是本轮固定视图的存在性观察；
   文件未提供不证明历史上从未执行过相应生成器。
3. 包含链的入口是 `shared_worker_info.mojom.h:29`，即闭包根
   `shared_worker_info.mojom-shared.h` 的**完整绑定兄弟**：

```
content/shell/browser/shell_content_browser_client.h:15
content/public/browser/content_browser_client.h:95
gen/third_party/blink/public/mojom/worker/shared_worker_info.mojom.h:29
gen/third_party/blink/public/mojom/loader/fetch_client_settings_object.mojom.h:29
gen/third_party/blink/public/mojom/frame/policy_container.mojom.h:32
```

本轮补的 6 项满足的是 `-shared` 那一半，编译已经走过它，进入了 `.mojom.h` 那一半。

关键在于：026 / 027 / 028 跑的 stage 全是 `shared__generator`，它只产 `-shared`
家族（以 `integrity_policy` 为例：`-data-view.h`、`-features.h`、`-params-data.h`、
`-send-validation.h`、`-shared-internal.h`、`-shared.cc`、`-shared.h`），
**不产 `.mojom.h`**。固定图给该完整头指定的 normal owner 是
`//services/network/public/mojom:mojom_integrity_policy__generator`（rule 15998 / build 16003），
声明五项产物。这三份 shared 回执不能提供它；本轮尚未查全其他具名历史回执，
因此不把结论扩大成“任何已接受产物里都没有”。本轮停线的充分理由是：
第一份有效 M 的新缺失头在冻结 61 项之外，满足本包停止条件。

完整绑定兄弟的后续依赖应在下一包按实际 include 与固定图盘点；
上面已经观察到若干同目录 `.mojom.h` 存在，不能预先把它们列为未供。

## 6. 这一轮不证明什么

- 不证明 TU 可编译、不证明 CXX 语义正确、不证明可链接、不证明浏览器或 network
  service 可运行。
- 不证明 414 个未选入产物与任何前缀冲突——这一点没有测，也就不写。
- 不证明 `.mojom.h` 家族只需要 `integrity_policy` 一项；只证明它是下一个缺口。

## 7. 计数

| 层 | 本轮 |
|---|---:|
| ACTION | 0.000 |
| worker | 153.867331009 |
| entry | 190.386553445 |
| outer seal | 195.848 |

clang：1 起、123.926678127。四层分开记，不合并成一个"总耗时"。

argv：336 → 337，新 `-I` 插在 028 前缀之后、`-I../source` 之前；删除它之后
得到与基线等长的 336 项，其中 334 个 token 逐字相同，另两项是 crash 目录与 `-MF`
的输出值变化；它们的下标移动与值变化分别核对。

原生 Chromium 从未运行；本次 M 不证明 CXX、链接或浏览器可用。
