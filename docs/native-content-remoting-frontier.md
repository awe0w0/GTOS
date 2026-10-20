# 原生 Content remoting mojom 前沿：当前状态

本文件只写**现在的事实**。历史轮次的结论不再堆在正文里；被撤回的说法见文末引用。

**范围**：五个真实 GN ACTION 已串行执行并全部 rc0，`remoting.mojom-forward.h` 已产出；真实
Content `-M` 已运行，首错从第 60 行前进到第 61 行，remoting 头已被真正消费。CXX 编译、链接、
`content_shell`、原生浏览器、socket/传输/DNS/接口名服务/内核网络线格式**均未运行**。

## 1. 首错（真实 `-M`，本轮已前移）

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:61:10: fatal error:
  'media/mojo/mojom/speech_recognizer.mojom-forward.h' file not found
```

同一文件同一位置，上一轮是**第 60 行**的 `remoting.mojom-forward.h` 未找到。第 60 行现在解析
成功、诊断前移到第 61 行，这是本轮产物被真实消费的直接证据，不是"文件非空"的推断。

`-M` 返回 1 是诊断完成，不是基础设施失败：未超时、未被杀、日志未超限、后代回收已量测。
它只证明依赖扫描走到第 61 行，不证明任何 CXX 成功。

## 2. 要得到这个头，需要 5 个 ACTION，不是一个也不是三个

图 `toolchain.ninja`
（45456305 B，`a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`）。
顺序即依赖序，本轮已按此串行执行：

| # | ACTION | 图行 | 本轮产物（字节） |
|---:|---|---|---:|
| 1 | `//media/mojo/mojom:remoting_common__parser_action` | rule 12493 / edge 12498 | `remoting_common.mojom-module` 7742 |
| 2 | `//media/mojo/mojom:remoting__parser_action` | rule 12452 / command 12453 / edge 12457 | `remoting.mojom-module` 261323 |
| 3 | `//media/mojo/mojom:remoting_common__type_mappings` | rule 12503 / command 12504 / edge 12508 | `remoting_common__type_mappings` 128 |
| 4 | `//media/mojo/mojom:remoting__type_mappings` | rule 12463 / command 12464 | `remoting__type_mappings` 244575 |
| 5 | `//media/mojo/mojom:remoting__generator` | rule 12442 / command 12443 / edge 12447 | 12447 行声明的 5 项 |

generator 的 5 项有序输出（本轮实际字节）：

```
gen/media/mojo/mojom/remoting.mojom-forward.h           708
gen/media/mojo/mojom/remoting.mojom-import-headers.h    781
gen/media/mojo/mojom/remoting.mojom-test-utils.h       5798
gen/media/mojo/mojom/remoting.mojom.cc               125029
gen/media/mojo/mojom/remoting.mojom.h                 28455
```

九个文件全部是普通非空文件，无符号链接，无覆盖既有源。本次五个 ACTION 的回执分别记录 rc0、
声明集合一致、扫除已量测与截止门通过。它们支持本次成功事实；聚合门对缺字段或矛盾记录的
通用拒绝能力仍需收紧，不能仅凭这次正常运行宣称执行器已完备。

`remoting_common__type_mappings` 的 command **没有任何 `--dependency`**，是纯叶子；
`remoting__type_mappings` 有 2 个（`mojom__type_mappings` 与 `remoting_common__type_mappings`）。

### 为什么 parser 两步也在里面

`mojom_bindings_generator.py`（`664123ac…`，19314 B）第 262–264 行会
`open(<output_dir>/<relative_path>-module)` 并 `Module.Load`；第 285 行的 `bytecode_path`
是模板字节码，是**另一个**输入，不能用来否定这次 open。第 390–399 行只遍历 rsp 里的
`args.filename`，所以 generator 需要 rsp 中每个 IDL 的 `<name>.mojom-module`。

`mojom_parser.py` 第 355–372 行对每个不在输入列表内的 import 同样 `open` + `Module.Load`
它的 module，第 374–378 行对 build_metadata 不允许的 import 直接 `ValueError`。所以
`remoting__parser_action` 还要求 `media_types`、`remoting_common`、`geometry` 三个 module
已存在；并且它启动时会按 `build_metadata` 的 `deps` 递归 `open` 68 份 metadata——这些必须
事先按摘要钉住，否则第一次生产会以 `FileNotFoundError` 停在 parser 里。

## 3. 两个 module 复用已核实来源，不重产

`media_types.mojom-module` 与 `geometry.mojom-module` 不新开 ACTION。来源已核实，不是
"位于已验收视图就自动合格"：

| logical（`gen/` 下） | 字节 | 固定输出 SHA |
|---|---:|---|
| `media/mojo/mojom/media_types.mojom-module` | 188587 | `255a474040abd55882eab580471dfd9fedd7117826b6e4dea510b5132681da48` |
| `ui/gfx/geometry/mojom/geometry.mojom-module` | 13017 | `950acc48759b0470ecae216303d94c412a438f7accdbacf3abe15287197184e9` |

两者在 `native-content-mojom-core-consumer-20261019-a/run-10/build/gen/` 下的路径
`readlink -f` 均指向 `native-content-interfaces-20261018-a/run-02/build/gen/` 的对应 logical。
权威回执 `run-02/status.json`：3109882 B /
`4d92a6f30f40e9aafc82ed11334c4954d63713e30702e4371a3ce2fb8051ca17`；该期望来自已审 002 的
`prepare-r6-01/pins.json` 里的 `u_status` 绑定。生产它们的两个 parser stage 都是
completed / rc0（`media_mojo_mojom_media_types__parser_action` 与
`ui_gfx_geometry_mojom_mojom__parser_action`）。

边界：这不等于重跑 parser，也不声称重验了全部历史传递源。

## 4. M 计划相对基线的差异

基线是上一轮真实 `-M` 的回执，argv 323 项。本轮插入一个新 `-I`（只指向本轮 5 个 normal 头，
不含 module 与两份 map），得到 324 项：

- 逐元素比较后，**唯一的两处变化**是 crash token 与 `-MF` 的值；插入位 118。
- 没有 `-MG`，没有改宏、目标三元组或优化级别。
- 旧前缀按序 012(4)→013(250)→014(9)→015(5)→016(170) 全部重读并与各自已验收记录一致。

## 5. 执行链与小例的准确层级

本次执行回执记录两层回收、绝对截止、角色模块导入前后绑定、共享组只打内部 PID、各 stage
的 `cleanup` 与 `deadline`、producer rc0及清理开始时刻。集中小例覆盖其列明的路径，真实生产
补充正常路径证据；它们不证明所有缺失或矛盾记录都会被现有聚合门拒绝。

- 最近一轮（017-r3）：15 条集中小例，全部 `ok=true`，入口 `final.code = 0`。
- 本轮（018）：15 条，只覆盖新增/受影响路径（多 stage 聚合、缺前驱拒绝、真实图 token 的
  `$:` 还原、M 前缀只取 normal 头），全部 `ok=true`。
- 再上一轮：28 条，但那一轮整体被否决，其结论不能当已证明。
- 016 的 47 条：历史记录，本轮未重跑。

这些数字描述的是**小例**；真实 generator 与真实 `-M` 的结论只来自对应生产回执。

关键证据在仓库外的 `artifacts/native-remoting-mojom-production-20261020-a/`：

| 回执 | 字节 | SHA256 |
|---|---:|---|
| `c18-produce-03/status.json` | 126523 | `d106314db66b50f99aa03a2abd35f62194b6bb7d67ae96d8d0c6cd63ed0a9b4f` |
| `c18-M-01/status.json` | 44538 | `d752ebcc84a44d11b5ddd49b99f30a08334a4d6038da92b666bf80b380dd07ff` |
| `c18-prepare-03/closure-018.json` | 32071 | `900dc8c9c3c2f8581a43a06449eed91d9f06c981404523a188aa19e5fbedc47f` |
| `revision-018/manifest-018.json` | 21845 | `261cef2eb0e2c14ef5a6dcc8d43bd52010d2c4b0750a6d0565e01d61a403576d` |

两个成功 OUT 各自的 `source-snapshot/` 保存实际角色脚本和 job；生产时与 M 时的 worker、plan
字节不同，不能用当前同名脚本替代历史快照。仓库本身不足以独立复现这些运行。

`closure-018.json` 的 80 项是暂存的生成侧输入，包含 metadata、RSP、config、module、map及模板；
它们不是全部消费来源清单。源 IDL 和导入的 Python runtime 来自既有只读 source 视图，未包含在
这 80 项中；工具摘要虽被记录，生产释放前未逐项按该工具表强制比对。本次不能声称所有被读源
与 runtime 都经过独立的前后内容绑定。末端独立摘要复读覆盖本轮 9 个新增输出。

## 6. 未运行项

以下全部 **NOT_RUN**，不得从本文件推断：

- `//media/mojo/mojom:remoting_common__generator`：条件未成立（见第 7 节），未执行；
- Content CXX 编译、链接、`content_shell`、原生浏览器；
- socket、传输、DNS、接口名服务、内核网络线格式、地址转换函数行为；
- GN/Ninja 再生成、整个 `media`/`blink`/`content` 构建；
- 内核/ISO/guest、显示 VM、新平台 ABI、新目标模式。

## 7. 下一个前沿

第 61 行要的 `speech_recognizer.mojom-forward.h` 属于 `//media/mojo/mojom:web_speech_recognition`
一族（generator 边 12902、parser 边 12912、message-ids 边 12893），**不是** `remoting_common`。
本次新首错不满足追加 common generator 的条件，所以没有执行该动作；这不证明后续永远
不需要 common 的其他产物。继续时应从 speech 一族的有序输出与输入闭包开始，不要重跑本文件
第 2 节已成功的五个 stage。

## 8. 已撤回的说法

早期版本在本文件里写过三句错的：「generator 不需要 `.mojom-module`」「链条是三步」
「三组修复全通过」。三句都已撤回，理由与实测见返修报告；本文件不再重复那些矛盾段落。
