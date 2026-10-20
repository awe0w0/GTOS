# 原生 Content 网络 mojom 四台生成器的复用与 include 前沿

022 把范围从十二阶段方案收窄为四台 normal generator：`mojom_proxy_config__generator`、
`cookies_mojom__generator`、`mojom_web_sandbox_flags__generator`、`websocket_mojom__generator`。
四台各释放一次并真实执行一次，全部返回 0，声明产物 20+30+5+5=60 个。真实 Content `-M` 用
328 项 argv 对同一个翻译单元执行依赖扫描，越过四个缺失的 forward 头，停在
`storage/browser/file_system/file_system_context.h:30`：缺
`components/services/storage/public/mojom/quota_client.mojom.h`。
本次 M 为 RAN_RC1；八个 parser/type_mappings ACTION、Content CXX、链接与原生 Chromium均未运行。

## 1. 归属、输入与产物

固定 GN 图为仓库外 `native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`，
45456305 B / SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。
四条 build edge 分别在第 16716、15150、17350、17524 行，每台生成器覆盖 RSP 中的一整个 IDL 组，
没有裁成单个 IDL。

| 生成器 | 声明产物 | 必需输入 | 其中历史复用 | rc |
|---|---:|---:|---:|---:|
| `mojom_proxy_config__generator` | 20 | 63 | 58 | 0 |
| `cookies_mojom__generator` | 30 | 67 | 56 | 0 |
| `mojom_web_sandbox_flags__generator` | 5 | 5 | 3 | 0 |
| `websocket_mojom__generator` | 5 | 72 | 64 | 0 |

暂存输入并集为 105 项：85 项历史复用，另 20 项当前观测（16 个 metadata、4 个 generator RSP）。
85 项中，41 项由 interfaces `run-02/status.json` 的 `outputs` 记录，44 项由
`reused_qualified_outputs` 记录；本轮绑定当前消费字节与历史摘要，没有重新资格验证整个上游链。
消费闭包另绑定 3 个工具与 170 个 runtime。

模板 `gen/mojo/public/tools/bindings/cpp_templates.zip` 已包含在上述 85 项中，不能再加算一项。
其大小 574664 B，SHA256 为 `4763b1927c982adf683e7517d3fa650b9eea4f2629f4914e99fc37cbb070fe39`。
生成器通过 `--bytecode_path gen/mojo/public/tools/bindings`，按
`GetTemplatePrefix() + '.zip'` 选择模板包；C++ 前缀是 `cpp_templates`。它是真实的间接文件消费，
不只是为了让复用组非空而列出的成员。

| 本轮 M 所需产物（`gen/services/network/public/mojom/`） | 字节 | SHA256 |
|---|---:|---|
| `proxy_config.mojom-forward.h` | 1775 | `453d6781e888527aa731e8057ceafe55a9142f951b8ae867e71879695459da89` |
| `restricted_cookie_manager.mojom-forward.h` | 1417 | `12893c7e16d5d11eb1769ecbf6bd227338f0cc0d10dec587a97263c01788779a` |
| `web_sandbox_flags.mojom-forward.h` | 619 | `d839ea2949411c01931785ddefa7ffc8ed28c80cc11b105dfcb6b9e06e8bb27e` |
| `websocket.mojom-forward.h` | 1325 | `47c2edeced3b3d93ad7741999e61de4c30566e91cd9bbb745393d884b70704d8` |

60 个声明产物均已产生，末端按产生时记录逐个重读；缺、多、空、非常规文件各 0。
生产树的实际文件数随四台累加为 83 / 139 / 148 / 165，最后精确等于 105 个暂存输入与
60 个新产物的并集。M overlay 的 60 项也与生产回执摘要一致；这不证明编译器读了全部 60 项。

## 2. 同轮前驱表与集中案例

四台 generator 的 module、map、模板由历史产物满足，本轮没有同轮前驱。
冻结文档的 `order` 为四个 stage 名，`table` 保留四个阶段键，每行是显式空字典，计数均为 0。
整个 `table={}`、字段缺失、`None`、缺行、行非表、错序仍被拒绝；合法四行空字典并不允许缺表。
早先十二阶段准备失败于叶子 parser/map 的空 reuse 组；那不是本轮这份四阶段表的成功或失败证据。

生产释放已删除“闭包表或重算表”的回退。上述形状负例在首个 ACTION 前按名拒绝，四阶段正控
释放 4 次。父侧先核对行类型再计数，`None` 行得到具名拒绝，而不是 `len()` 的 TypeError。
独立图重算与冻结表在本次真实四阶段运行中相等；该相等项当时只是回执记录，不能声称不相等已被
生产释放门拒绝。

021 的非零前驱数据由新门只读回放：独立期望 39 项，核对 39 项；缺成员、缺行、改摘要均被拒绝。
正控上游、下游各释放 1 次。两个下游负例没有增加下游释放计数；案例中的 `counts_after` 是包含
先前正控的累计值，不能把它当作每个负例单独的调用数。这里是释放函数回放，没有重跑 021 ACTION。

最终集中案例 `out-022-r2-fixtures-03` 为 36/36。前两次 fixtures 分别为 25/30、35/36，返回非零；
另有一次 preflight 因计划内历史回执摘要抄错一位而在 clang 前拒绝。九次受控入口中六次成功、
三次失败，均保留原记录；不能把六次成功概括成全轮没有失败。

## 3. 真实 Content -M

基线为 021 的 327 项 argv，在 021 前缀之后、`-I../source` 之前增加一个 `-I`，得到 328 项。
移除新前缀后相对基线仅 crash token 与 `-MF` 路径变化，基线坐标 `[209,325]`。
未加 `-MG`，未改宏、目标或优化。九个历史前缀合计 663 成员
（4 / 250 / 9 / 5 / 170 / 5 / 25 / 5 / 190）复读一致；新前缀只收本轮 60 项。
四条负对照（抬高包含根、改开关、改宏、加 `-MG`）被拒绝。

clang 真实启动一次，129.871 秒、rc1，643 B 日志只有一条错误：

```
In file included from ../source/content/shell/app/shell_main_delegate.cc:36:
In file included from ../source/content/shell/browser/shell_content_browser_client.h:15:
In file included from ../source/content/public/browser/content_browser_client.h:83:
../source/storage/browser/file_system/file_system_context.h:30:10: fatal error:
  'components/services/storage/public/mojom/quota_client.mojom.h' file not found
```

021 停在 `content_browser_client.h:77` 的 proxy_config；本轮第 77、78、80、82 行所需的四个
forward 头均已解析，进入第 83 行包含的 storage 头，在其第 30 行失败。
这证明需要的四个 forward 头被实际消费，不证明整组 60 文件均被读到或 TU 编译通过。
入口判定码 0 表示诊断完成且入口检查通过；clang rc1 仍是 M 失败。超时、信号退出、绑定变化或
回收失败另有拒绝门，不能把“非负返回码”单独当作入口成功保证。

新缺失头的真实 owner 是 `//components/services/storage/public/mojom:mojom`，不是
`:quota_client`。固定图的 normal edge 5795 声明 55 项，shared edge 5827 声明 77 项。
这里需要完整 `.mojom.h`；其 shared 配套和导入头的实际依赖仍需下一轮闭合，本轮未产出或验证它们。

## 4. 回执定位与绑定边界

证据目录为仓库外：
`F:/GTOS-Chromium/artifacts/native-network-include-cohort-20261020-a`。下表路径相对该目录；
仓库本身不含这些脚本、固定 GN 图或生成物，不能仅凭此文档独立复现。

| 对象 | 相对路径 | SHA256 |
|---|---|---|
| 准备闭包 | `out-022-r2-prepare-01/closure-022.json` | `895c89eaa1187de69dbd5abc8606b941f569fccc811b6c2115d712b053a7517e` |
| 四台生产 | `out-022-r2-produce-01/status.json` | `2113cba865bcb2999abc3b865752d75d7b228e25fb6757338896a6865985744b` |
| 最终案例 | `out-022-r2-fixtures-03/status.json` | `a717ada698532425bd6402c106ea72ce0674a85e84765569d048754bdaa69c1a` |
| 接线预检 | `out-022-r2-preflight-02/status.json` | `21559d2ab71abe7360c7211e51eedd48d2279c54a59601f994ef3a37887210f1` |
| M 状态 | `out-022-r2-M-01/status.json` | `f4720664c9407daec2094c93905b507bf1a2c3d0c2b9b0aa3047c47cb36bd57b` |
| M 日志 | `out-022-r2-M-01/delegate-M.log` | `012080bc2a595c8a389be2f478c3ac701af5309d7b0a07b0e5e97c6135116448` |
| 完整长表 | `revision-r2/manifest-022-r2.json` | `6705220b116015085159a02bf7244d823f1a1ff0a2dfb806a46f1d2114f008c6` |

plan 与 cases 曾在执行后同名改写。各次 OUT 的 `source-snapshot` 保留匹配当时摘要的源码字节；
历史绑定应以各自快照为准，不能用包根当前同名文件冒充旧运行版本。
prepare/produce 的 plan 快照含一位抄错的 021 回执摘要，之后 preflight 拒绝该常量并修正；
两份 plan 的代码差异只有这个摘要常量，成功 M 使用修正版本。快照保留使证据可追溯，
不使执行后同名改写变成合规做法。

## 5. 未运行与限制

- 十二阶段方案中的八个 parser/type_mappings ACTION 本轮未运行；85 项历史复用已含模板。
- 021 结果是历史引用，本轮没有重跑；022 新增的门不回填为 021 当时执行的检查。
- 本轮 M = RAN_RC1；Content CXX、链接、content_shell、原生 Chromium、guest、显示均 NOT_RUN。
- 没有实现 socket/传输/DNS/接口名服务，没有改变 ABI；32×32 / 4096 字节上限未改。
