# 原生 Content speech mojom 生成与消费

speech 一族的 normal generator 已真实执行一次，25 个声明产物全部生成。019 的真实 Content `-M`
越过 `content_browser_client.h:61` 所需的 speech 头，首错到达第 70 行。020 增加 cert_verifier
产物后，首错进一步到达第 76 行的 `network_context.mojom-forward.h`。这些运行均为依赖扫描，
不是 CXX 编译或浏览器运行。

## 1. 真实动作与产物

固定 GN 图为 `native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`，
45456305 B / SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。

| ACTION（`//media/mojo/mojom`） | 声明输出 | 019 处理 |
|---|---:|---|
| `web_speech_recognition__parser_action` | 5 个 module | 复用已验收 interfaces 回执 |
| `web_speech_recognition__type_mappings` | 1 个 map | 复用已验收 interfaces 回执 |
| `web_speech_recognition__generator` | 25 个 normal 文件 | 真实执行，rc0 |
| `web_speech_recognition__generate_message_ids` | 5 个文件 | 本次消费链未要求，未执行 |

5 个 IDL 基名依次为 `speech_recognition_error`、`speech_recognition_error_code`、
`speech_recognition_grammar`、`speech_recognition_result`、`speech_recognizer`。每个产生
`-forward.h`、`-import-headers.h`、`-test-utils.h`、`.cc`、`.h`，合计 **20 个头和 5 个 .cc**。
不能把 25 项统称为头文件。

目标 `gen/media/mojo/mojom/speech_recognizer.mojom-forward.h` 为 1139 B / SHA256
`95d84b4979dbd1a778b571bdb4a3157436141628d62f7e505dff00e82bd8b47d`。25 项生成文件及其 M
叠加副本均与生成时记录一致；这没有证明 25 项都被编译器打开。

5 个 module 与 1 个 map 引用 `native-content-interfaces-20261018-a/run-02/status.json` 的固定
输出/复用记录，该回执 SHA256 为
`4d92a6f30f40e9aafc82ed11334c4954d63713e30702e4371a3ce2fb8051ca17`。本次未重跑它们，也未
重新验证其全部历史传递来源。生成器需要真实 module 和 typemap，不能用空文件替代。

## 2. M 的实际结果

019 的 M argv 为 325 项，在 018 的 324 项基线上只增加 speech 的一个 `-I`。移除该前缀后，
逐元素差异只有 crash 输出路径和 `-MF` 的值，基线坐标为 `[206,322]`。没有增加 `-MG`，没有改宏、
目标三元组或优化级别。

019 的 include 链和首错为：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:70:10:
  services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom-forward.h not found
```

因此第 61 行所需的 speech 头已经解析。不能据此证明第 62–69 行每个条件 include 都被打开。
020 的后续扫描保持 speech 前缀，新增 cert_verifier 前缀后到达第 76 行，详见
[cert_verifier 记录](native-content-cert-verifier-frontier.md)。两次 clang 均为 `-M`、rc1。
入口成功表示受控诊断完成、记录首错并完成检查，不表示翻译单元编译成功。

## 3. 输入绑定与执行门的证据层级

019 释放时实际核对了入口工具摘要，末端复读了 57 项暂存输入、3 个工具、25 项输出。缺口是没有
强制的必需集合、缺少工作区 Python runtime 绑定，以及 worker 的阶段期望列表为空时跳过顺序检查。
不能把它描述为“工具完全没有参与释放”或“末端只看新输出”。

019-r2 通过同一生产函数的小例修正了必填计划解码和输入表漏项检查，并绑定 170 个 runtime：
169 个来自实际 source view，1 个来自契约指定的串行 parser。5 个根 IDL 只作为声明/来源证据，
该轮 parser 未运行。其闭包来自受控小例内调用同一 `run_prepare`，没有独立 prepare 模式的成功回执。

019-r2 的 22 条记录均通过，但只有计划组实际量测 spy 次数；其余组是门函数返回值。其入口字节
负例实际命中了路径身份门。020 的 38 条小例另行区分了同路径改字节、原字节副本、未绑定路径；
020 的真实 cert_verifier 生产也接入了必填阶段列表和父侧后验。不得把新证据回填给旧运行。

冻结期望应来自准备阶段或历史回执；合法副本的摘要应与它相等。当前再读的摘要不能替代旧期望。
生成后与末端的摘要相等只提供两次观测之间的稳定性证据，不是预知输出内容的独立摘要。

020 执行门仍有边界：父侧声明集合的组装依赖收到的 `stage_records`，尚未完全消除阶段记录缺失时
缩小检查范围的可能；同轮前驱产物在下游释放前只重新取当前摘要，尚未对照前驱生成时记录。
这些缺口不撤销已观察的生成/M 事实，后续释放新动作前应修复。不得声称整套执行框架已完整验收。

## 4. 回执与复现边界

以下路径均位于仓库外 `F:/GTOS-Chromium/artifacts/native-speech-mojom-frontier-20261020-a/`。

| 回执 | 字节 | SHA256 |
|---|---:|---|
| `c19-produce-01/status.json` | 144240 | `e8aff680965535d04cdec094ec35ac1a36947e6b0eec23b50dd87c96895e70c2` |
| `c19-M-01/status.json` | 61340 | `5f2e454d5917661833138c616576504e5905fd0c71357bdc69bc630107c1278e` |
| `revision-r2/out-019-r2-cases-02/status.json` | 29118 | `18f58b5c523dbead63de1b587b5f7fe48971ec59dfc4c7cdb4d424e9c65ac538` |
| `revision-r2/out-019-r2-cases-02/closure-fixture/closure-019-r2.json` | 276302 | `dc1d14c9d6314418c26835921dce8c700c99b8ff6d3d3bcbba477f54b1d2bf64` |

脚本、工具链、固定 GN 图、历史缓存与产物均在仓库外。仅凭本仓库不能独立复现上述运行；匹配源码
字节以对应 OUT 的 `source-snapshot/` 为准，不能用当前同名脚本或仅有摘要的 job 替代历史源码。
未修改的历史报告和账本中的偏差，另有审核记录更正。

未运行 Content CXX、链接、content_shell、原生 Chromium、网络传输/DNS/接口名服务、内核网络
线格式或地址转换行为；未重新生成 GN/Ninja 图。32×32 表面与 4096 字节限制未变，不构成浏览器
尺寸表面的资格。
