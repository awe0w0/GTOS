# 原生 Content network_context mojom 生成与消费

021 完成了 `//services/network/public/mojom:mojom` 的 parser、type_mappings、normal generator 三种
原始 GN ACTION。两个生产批次各启动三次 ACTION，均返回 0；每批产生 229 个文件。真实 Content
`-M` 已解析第 76 行所需的 `network_context.mojom-forward.h`，首错到达第 77 行缺
`proxy_config.mojom-forward.h`。依赖扫描仍返回 1；Content CXX、链接和原生 Chromium 均未运行。

## 1. 归属、输入与产物

固定 GN 图为仓库外 `native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`，
45456305 B / SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
normal generator 的 build edge 在第 15239 行，覆盖 RSP 中的 38 个 IDL，不只是一个 IDL。

| ACTION（`//services/network/public/mojom:mojom`） | 每批产物 | 第二批 rc / 秒 |
|---|---:|---:|
| `mojom__parser_action` | 38 个 module | 0 / 21.712 |
| `mojom__type_mappings` | 1 个 map | 0 / 1.628 |
| `mojom__generator` | 190 个 normal 文件 | 0 / 17.813 |

190 个 normal 文件为 152 个头与 38 个 `.cc`。两批的 229 个生成文件逐字节相同。第二批生产树有
385 个普通非空文件，精确等于 156 个暂存输入与 229 个声明产物的并集。

消费闭包还绑定 3 个工具和 170 个 runtime。79 个复用 module、29 个依赖 map 引用固定 interfaces
回执的 `reused_qualified_outputs`，本轮核对当前字节与记录摘要，不等于重验全部上游传递来源。
parser 与 generator 的 RSP 均为 2485 B / SHA256
`829353210e044c01b5f1d14c7b181549ea2c8852234af7beeb41d47cff57b1f9`。
mapping 消费 29 份依赖 map 与两份 config。message-ids 和 shared generator 未运行；后续 CXX
需要按真实消费关系另算必要集合。

| 关键产物（`gen/services/network/public/mojom/`） | 字节 | SHA256 |
|---|---:|---|
| `network_context.mojom-forward.h` | 3367 | `7e4232f9f3ed141470a2df384fd5bdf347568f54e423c570bd2890fa9723e9c4` |
| `network_context.mojom.h` | 260473 | `39644951bccf77411bf07579d25fd47721587c44e100f82530d75c1310a8ea62` |
| `network_context.mojom.cc` | 1065748 | `7af252749215800f13abdedfcd467031e29528a6474dc4451f9cb07e8673f2f4` |
| `mojom__type_mappings` | 139049 | `5438a90c5bf68af89fc9169c15ca1b2c4e06c73b6ac098ec4c8390a31276f95d` |

## 2. 释放门、父侧后验及其边界

worker 按冻结 spec 的依赖与声明找到唯一已通过的生产者，固定使用其生成时
`outputs[logical].sha256`，在下游释放前核对当前字节。generator 本轮实际消费的同轮前驱为
38 个 module 与一个 map，共 39 项。当前 SHA 是观测值，不能替换生成时权威。

声明全集现在从冻结的有序阶段表与闭包 `declared` 展平；父侧再核 worker 记录的数量、标签、
唯一性、顺序和每阶段声明/产物两向对应。缺阶段记录不能缩小声明范围。

父侧 `intra_authority` 第一版查错嵌套层级，返回 `ok=true` 却没有检查任何条目。v2 修正取表位置，
第二批父侧回执实际核对 39 项；末端另复读 156 暂存输入、3 工具、229 输出与 170 runtime。
59 条集中小例全部通过，但这些结果不代表执行门已完整验收。

仍有一项后续执行前必须修复：生产父门仅遍历 worker 提交的 `intra_inputs`，没有独立冻结的必需
前驱全集。把 generator 的该表改成空表或删一项，现有父门没有相应的完整性比较；小例正控的
“39 且大于 0”断言没有进入生产父门。消费门也需要独立必需集来拒绝漏行。这里是静态审查发现的
限制，不是已经运行的新负例，不撤销本轮实际记录完整的 39 项检查。

`argv_differences` 改为从完整固定 command 逐 token 比较，区分 helper 省略、工具原样保留、
串行 parser 替换与 GN 转义还原。parser 使用已批准串行副本，工具未被移除。旧回执保持原样。

## 3. 真实 Content -M 与后续 owner

基线为 020 的 326 项 argv，本轮在 cert_verifier 前缀之后、`-I../source` 之前增加一个 `-I`，
得到 327 项，插入位 121。移除它后，相对基线仅 crash token 与 `-MF` 路径变化，坐标为
`[208,324]`。未增加 `-MG`，未改宏、目标或优化。旧八组前缀共 473 成员重读一致。

M 的 job 实际绑定第一次生产 `out-021-produce-01/status.json` 与其树，未绑定后来第二次生产。
M 的 190 个 normal 副本均与第一次生成时记录一致，也与第二批字节相同；后者不能改写历史绑定。
M 使用 gates v1，其路径不调用生产批次的父侧 `parent_aggregate`，不能把 v2 后验追溯给它。

clang 真实启动一次，128.249 秒、rc1；入口账本观测为 177.863 秒，status 自记为 177.883 秒。
548 B 日志只有一条错误：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:77:10:
  services/network/public/mojom/proxy_config.mojom-forward.h not found
```

因此第 76 行所需头已解析，不证明 190 个 normal 文件都被读取。入口判定码 0 表示受控诊断完成，
不表示翻译单元编译通过。同目录不能作为同一个 GN owner 的依据：

| include 基名 | normal generator 的 target / edge | 声明文件数 | 当前层级 |
|---|---|---:|---|
| `network_context`、`url_loader_factory` | `:mojom__generator` / 15239 | 190 | 021 已产生 |
| `fetch_api`、`ip_address_space`、`web_transport` | `:url_loader_base__generator` / 17435 | 250 | 013 已产生 |
| `proxy_config` | `:mojom_proxy_config__generator` / 16716 | 20 | 后续待处理 |
| `restricted_cookie_manager` | `:cookies_mojom__generator` / 15150 | 30 | 后续待处理 |
| `web_sandbox_flags` | `:mojom_web_sandbox_flags__generator` / 17350 | 5 | 后续待处理 |
| `websocket` | `:websocket_mojom__generator` / 17524 | 5 | 后续待处理 |

表中均为 `//services/network/public/mojom` 下的 target。未处理项需分别核实输入闭包、来源与实际
缺失情况，不能把“路径相邻”写成“同一动作可以补齐”，也不能保证补齐后 M 或 CXX 就能通过。

## 4. 回执与方法边界

以下文件位于仓库外 `F:/GTOS-Chromium/artifacts/native-network-context-mojom-frontier-20261020-a/`。

| 回执 | 字节 | SHA256 |
|---|---:|---|
| `out-021-prepare-01/closure-021.json` | 495439 | `5acd3e8f5ae6d2f16c2080ff34e63b0c46a35d70415f1567a67f8e8258a8f5e9` |
| `out-021-produce-01/status.json` | 1150953 | `f74c347ab49e25a8cad34f80d1eb5cbf6c9f9414d8e49a76179d16ce08d65d74` |
| `out-021-produce-02/status.json` | 1165807 | `225b905f767ecb7dbea9ec8ee63fde9a71880a61d2366a23f7ae7e8c3838c1cd` |
| `out-021-cases-03/status.json` | 74535 | `38bb11e332b82f0afce77bc728ddbc1b439ac0b97b6eda0de0ab9a94a8fcf06d` |
| `out-021-M-01/status.json` | 204433 | `a4e5033b672ef39acf2a49bc2073d0c505561cc7de8c04d3adc3bc6c1b053a35` |
| `out-021-M-01/delegate-M.log` | 548 | `933d3fe53b709360420ee74ee32b45411a2d155b9b8fcf2f9017280be8a2bc1a` |

仅凭本仓库不能独立复现；工具链、固定图、历史缓存、脚本与匹配源码快照均在仓库外。

10 对 claim/close 覆盖 10 次固定入口，账本合计 1136.679 秒。两次 cheap 擅自把单次上限从
120 秒改为 450 秒，属于超授权，整体墙钟有余不能豁免。为验证修后的父侧后验又重跑三项已成功
ACTION 也没有必要；可用新门重放原回执与既有字节，不需要重新生成。本轮至少两处代码
Replace 写回；首次 check 后仍修改了同名 worker。该失败版本的匹配字节保存在其 OUT 快照中，
不等于当时遵守了源码冻结。旧回执与账本不回填。

未运行 Content CXX、链接、content_shell、原生 Chromium、socket/传输/DNS/接口名服务、内核
网络线格式或地址转换；未重新生成 GN/Ninja 图、内核、ISO、guest 或显示 VM。32×32 表面与
4096 字节限制未变，不构成浏览器尺寸表面资格。
