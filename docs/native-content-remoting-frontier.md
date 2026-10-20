# 原生 Content remoting mojom 前沿：当前状态

本文件只写**现在的事实**。历史轮次的结论不再堆在正文里；被撤回的说法见文末引用。

**范围**：本轮与上一轮都**没有生产** `media/mojo/mojom/remoting.mojom-forward.h`，也没有
运行 Content `-M`。CXX 编译、链接、`content_shell`、原生浏览器、socket/传输/DNS/接口名
服务/内核网络线格式**均未运行**。本文件全部来自固定 GN 图、固定源与已验收视图的只读核对。

## 1. 首错（016 真实 `-M`，未变）

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:60:10: fatal error:
  'media/mojo/mojom/remoting.mojom-forward.h' file not found
```

`media_service.mojom-forward.h` 已在更早一轮解析；这不证明整个 `media_service` 目标编译完成。

## 2. 要得到这个头，需要 5 个 ACTION，不是一个也不是三个

图 `toolchain.ninja`
（45456305 B，`a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`）。
顺序即依赖序：

| # | ACTION | 图行 | 产物 |
|---:|---|---|---|
| 1 | `//media/mojo/mojom:remoting_common__parser_action` | rule 12493 / edge 12498 | `remoting_common.mojom-module` |
| 2 | `//media/mojo/mojom:remoting__parser_action` | rule 12452 / command 12453 / edge 12457 | `remoting.mojom-module` |
| 3 | `//media/mojo/mojom:remoting_common__type_mappings` | rule 12503 / command 12504 / edge 12508 | `remoting_common__type_mappings` |
| 4 | `//media/mojo/mojom:remoting__type_mappings` | rule 12463 / command 12464 | `remoting__type_mappings` |
| 5 | `//media/mojo/mojom:remoting__generator` | rule 12442 / command 12443 / edge 12447 | 12447 行声明的 5 项 |

generator 的 5 项有序输出：

```
gen/media/mojo/mojom/remoting.mojom-forward.h
gen/media/mojo/mojom/remoting.mojom-import-headers.h
gen/media/mojo/mojom/remoting.mojom-test-utils.h
gen/media/mojo/mojom/remoting.mojom.cc
gen/media/mojo/mojom/remoting.mojom.h
```

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
已存在。

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
`ui_gfx_geometry_mojom_mojom__parser_action`），worker 回执见返修报告的清单。

边界：这不等于重跑 parser，也不声称重验了全部历史传递源。

## 4. 执行链与小例的准确层级

执行链（两层回收、绝对截止、导入前后绑定、共享组只打内部 PID、`action_cleanup` 进入最终门、
producer 要求 rc0、清理窗口按扫除开始时刻判定）由集中小例覆盖，不是由真实生产证明的。

- 最近一轮：**15 条**集中小例，全部 `ok=true`，入口 `final.code = 0`。
- 再上一轮：28 条，但那一轮整体被否决，其结论不能当已证明。
- 016 的 47 条：历史记录，本轮未重跑。

这些数字描述的是**小例**，不是真实 generator、真实 clang 或真实 `-M`。

## 5. 未运行项

以下全部 **NOT_RUN**，不得从本文件推断：

- 上述 5 个 ACTION 的任何一次真实执行；`remoting.mojom-forward.h` 仍未产出；
- Content CXX 编译、链接、`content_shell`、原生浏览器；
- Content `-M`；
- socket、传输、DNS、接口名服务、内核网络线格式、地址转换函数行为；
- GN/Ninja 再生成、整个 `media`/`blink`/`content` 构建；
- 内核/ISO/guest、显示 VM、新平台 ABI、新目标模式。

## 6. 已撤回的说法

早期版本在本文件里写过三句错的：「generator 不需要 `.mojom-module`」「链条是三步」
「三组修复全通过」。三句都已撤回，理由与实测见返修报告；本文件不再重复那些矛盾段落。
