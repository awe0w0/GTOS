# 原生 Content cert_verifier mojom 生成与消费

020 对 `//services/cert_verifier/public/mojom` 执行了 3 个原始 GN ACTION，均返回 0，共生成 7 个
文件。真实 Content `-M` 的首错从 `content_browser_client.h:70` 推进到第 76 行缺
`services/network/public/mojom/network_context.mojom-forward.h`。Content CXX、链接和浏览器
均未运行。

## 1. 真实归属、输入与产物

固定 GN 图为 `native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`，
45456305 B / SHA256 `a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`。

| ACTION | rule / command / edge 行号 | 声明及实际产出 | rc |
|---|---|---:|---:|
| `mojom__parser_action` | 14116 / 14117 / 14121 | 1 个 module | 0 |
| `mojom__type_mappings` | 14127 / 14128 / 14132 | 1 个 map | 0 |
| `mojom__generator` | 14106 / 14107 / 14111 | 5 个 normal 文件 | 0 |

目标源 `services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom` 为 6297 B /
SHA256 `88f1855282f324510efb259af40d09d70ebf7f68368a094d9f3a80ddd1c704d4`。该项未在已验收
契约的 sources 表内，准备时由 job 显式钉住并标为 `job_pinned_expectation`，不冒充历史资格。
另 10 份 IDL 对照已有来源记录。parser 的 metadata 递归走查 44 项，未发现缺失。

准备闭包去重后有 62 项输入、3 个工具、170 个 runtime。14 项复用成员来自固定 interfaces
回执的 outputs/reused_qualified_outputs：10 个 module、3 个 map、1 个模板归档。它们与回执
摘要相符，不等于重验全部上游传递来源。

本轮真正消费的 RSP 为 parser 与 generator 各一份，均 82 B / SHA256
`a4c5ba8498efa775a2683e2b3073d08412d1f669f1e39f1a614adb13a9a48d90`。两份 typemap_config
均为空数组；generator 仍明确消费 `mojom__type_mappings`，不能据此省略 mapping ACTION。

| 新产物（均在 `gen/services/cert_verifier/public/mojom/`） | 字节 |
|---|---:|
| `cert_verifier_service_factory.mojom-module` | 966611 |
| `mojom__type_mappings` | 137922 |
| `cert_verifier_service_factory.mojom-forward.h` | 1476 |
| `cert_verifier_service_factory.mojom-import-headers.h` | 1946 |
| `cert_verifier_service_factory.mojom-test-utils.h` | 2360 |
| `cert_verifier_service_factory.mojom.cc` | 63757 |
| `cert_verifier_service_factory.mojom.h` | 40113 |

forward 头 SHA256 为 `476e29d430fce21ff0f9ebdb7b8481c2bb3a0b83585f4f226661668dec3d2b4f`。
7 项均为普通非空文件；生产树为 62 暂存输入加 7 新产物，共 69 个文件。

`mojom__generate_message_ids` 与 `mojom_shared__generator` 本轮未执行：当前 forward 头的消费链
没有要求这些文件，本轮也未编译 .cc。shared 动作会生成共享类型/序列化相关文件，不能把它
概括为只供链接期使用。后续 CXX 应按真实 include 与动作依赖重新计算必要集合。

## 2. 实际执行与后验

3 个动作按 parser → type_mappings → generator 顺序执行，pid 分别为 682、684、686；各自都有
返回码、实际集合、清理和 deadline 记录。生产回执阶段列表的实际值与期望值均为这 3 项。
末端回执记录重读 62 暂存输入、3 工具、7 输出、170 runtime，结果一致。

工具入口按实际 argv 解析并核对摘要。parser 使用已批准串行副本，20249 B / SHA256
`13d0a43606013f06b0808e59a454557e05ffba895a9c218e8213e3acfc71cddb`；另两个动作使用图上
工具。GN helper 由已有受控 worker 调用方式替代。原回执 `argv_differences` 的首条注释错误地把
工具写为 removed；实际 argv、入口摘要和 rc 记录保留。后续应在新版本修正该说明字段，不改旧回执。

P0 修复已使 `required=None`、空表、缺组、worker 减少必需成员以及删掉一项输出记录产生拒绝。
父从 job 钉住摘要的闭包读取权威，生产 `produce_postcheck` 和小例共用 `parent_aggregate`。
020 的 38 条记录全过：闭包 1、计划 7、父侧 17、消费表 12、runtime 1。只有计划组量测 spy
调用次数，合法路径为 3 次、6 个非法计划各为 0；其余是门函数返回值，不是假造的进程执行证据。
入口字节、合法原字节副本、未绑定路径三种小例分别命中对应的判断。

仍存在两处后续执行前必须修复的范围：

- `parent_aggregate` 组装声明集合时仍遍历 worker 的 `stage_records`。即使 required 已有独立权威，
  声明范围仍未完全独立于记录子集；缺阶段记录与缺 required 表是不同的反例。
- generator 的两项同轮前驱产物单列为 `intra_inputs`。当前只取它们消费前的当前 SHA，未在释放
  下游前对照前驱生成时的固定记录。末端复读不能替代消费时检查，也不能排除改动后恢复的情况。

这些限制不撤销本次实际生成及 M 结果，但不支持“执行框架已完整验收”的结论。输出集合的权威
来自冻结 GN 声明；工具/输入绑定提供生产来源证据；输出两次读的摘要相等仅提供两时点稳定性。

## 3. 真实 Content -M

基线为 019 的 325 项 argv。本轮仅在 speech 前缀之后、`-I../source` 之前新增 cert_verifier 的
一个 `-I`，得到 326 项。移除新前缀后，逐元素差异只有 crash token 与 `-MF` 路径，基线坐标为
`[207,323]`。未加 `-MG`，未改宏、目标或优化。回执记录旧 7 组前缀共 468 成员核对通过。

clang 真实启动一次，119.625 秒、rc1，日志 557 B、1 条错误：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:76:10:
  services/network/public/mojom/network_context.mojom-forward.h not found
```

这证明第 70 行所需的 cert_verifier 头已解析，不证明第 71–75 行每个条件 include 都被打开，
也不证明 5 个 normal 产物全被读取。入口判定码 0 表示受控诊断完成；编译器 rc1 不表示 CXX 通过。
本轮停止在新的 network_context 前沿，未计算或执行该族闭包。

## 4. 回执与方法边界

回执根为仓库外 `F:/GTOS-Chromium/artifacts/native-cert-verifier-mojom-frontier-20261020-a/`。

| 回执 | 字节 | SHA256 |
|---|---:|---|
| `out-020-cases-02/status.json` | 43671 | `6184d511ce801518a08507ee57c028b8c98cf7467f706efe912d4ed024997258` |
| `out-020-prepare-01/closure-020.json` | 316475 | `b78e46ac46f5795dde4850fa1578990d9eb613486a73e4dde6bd95e08c8c6792` |
| `out-020-produce-01/status.json` | 618929 | `d27a4f261ffd634c26fe4465620f69e2a4224f1d1395f7506efde73420ec4a3b` |
| `out-020-M-01/status.json` | 48126 | `b3beeebc5ccfa619de0d5608243a001c61cb890b6c59e45b0cbcb34fd118ae98` |
| `out-020-M-01/delegate-M.log` | 557 | `0ce0b53167df68854a7a19d5e360d1690eee1de40ad0e42980d1ab1e485b367f` |

6 个受控 OUT 的 42 份源码/job 快照与记录匹配。仅凭本仓库不能独立复现：仍依赖仓库外工具链、
固定图、来源视图、历史缓存和执行脚本。执行后曾同名修改并恢复 v1 文件，恢复不等于当时遵守冻结。

原账本的 6 对 claim/close、301.735 秒只覆盖受控入口。轨迹还记录三批每批 6 次的裸
`python3 -m py_compile`，共 18 次未入账调用。因此全轮至少 24 次 Python 启动，cheap 至少
21/20，不能写“没有未入账项”或“整体预算合规”。旧账本不回填。首次小例中的逐 stage 权威形状
错误位于生产 `rebind_authority`，属于被测函数缺陷；另一个理由片段错误才属于测试代码。

未运行 CXX、链接、content_shell、原生 Chromium、socket/传输/DNS/接口名服务、内核网络线格式
和地址转换行为；未重新生成 GN/Ninja 图、内核、ISO、guest 或显示 VM。32×32 表面与 4096 字节
限制未变，不构成浏览器尺寸表面资格。
