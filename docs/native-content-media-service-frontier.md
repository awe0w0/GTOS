# 原生 Content media mojom 前沿：`media_service.mojom-forward.h` 的归属与产出

本文件记录 015 结尾那个缺失头的归属判定、它的原始 action 的真实执行结果，以及
016 那一轮第一次让 clang 读到这个头之后的下一步。目标头
`media/mojo/mojom/media_service.mojom-forward.h` **不是**源码树自有头，而是 GN 生成动作
`//media/mojo/mojom:mojom__generator` 的 170 个声明输出之一。该动作在已验收视图里
**一个输出都没有**，输入却齐备；本轮两次尝试均启动了生成器，其中
`produce-run-02` 取得动作 rc0 与完整回执，170 项全部取得。第一次尝试未取得动作退出码，
其失败目录中的产物不作为合格来源。

**范围与层级**：CXX 编译、链接、原生浏览器**均未运行**；socket、传输、DNS、接口名服务、
内核网络线格式与转换函数行为**均未扩展**。本轮 `-M` 真正跑起来（clang rc1、132.242秒，
入口总时长167.677秒、日志497 B），取得了新首错：所需目标头已解析，下一缺失头换成了
**另一个独立 target** 的产物。

## 1. 前沿是什么，以及它属于哪一类

015 结束时的真实首错（`native-origin-trials-mojom-frontier-20261019-a/m-run-01`，
clang rc1、130.083秒、日志512 B）：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:59:10: fatal error:
  'media/mojo/mojom/media_service.mojom-forward.h' file not found
```

包含路径不区分源码头与生成头，三个竞争解释逐项核对：

| 解释 | 结论 | 决定性依据 |
|---|---|---|
| A 已有合格产物，只是没接入 | **不成立** | 固定 source/build 视图 `native-content-mojom-core-consumer-20261019-a/run-10/build/gen/media/mojo/mojom/` 有 180 项，但真实 GN 图第 12358 行声明的 170 个输出**一个都没有**（`-forward.h`、`-import-headers.h`、`-test-utils.h`、`.mojom.cc`、`.mojom.h` 全缺）；目录里只有 `*.mojom-module`、`*__generator.rsp`、`*__type_mappings`、`*.build_metadata`、`*__parser_action.rsp` |
| B 真实 normal 动作未完成 | **成立** | 该 edge 的输入全部存在：`mojom__generator.rsp`（34 个 IDL 条目）、`mojom__type_mappings`、`mojom.build_metadata`、`mojom__parser_action.rsp`、`cpp_templates.zip`；34 个 `*-module` 与 34 个源 IDL 也都在。缺的是产物，不是输入 |
| C 源/图/版本/拼写错配 | **未全面排除** | 目标头的 owner、typed argv 与有序输出足以定位本次缺失产物；但完整源版本一致性未逐项绑定到历史合同，不能据此证明全部源/图版本一致 |

**同一目录下的相邻包含不自动同 owner**：`remoting.mojom-forward.h` 与
`speech_recognizer.mojom-forward.h` 都不在这 170 项里；图第 12442 行另有
`rule __media_mojo_mojom_remoting__generator___build_toolchain_gtos_clang_x86__rule`，
即 `//media/mojo/mojom:remoting__generator` 是**另一个** target。本轮不并跑、不预建。

## 2. owner、动作范围与原始 argv

动作来自固定 GN 图 `native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`
（45456305 B，`a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`）：

```
rule  __media_mojo_mojom_mojom__generator___build_toolchain_gtos_clang_x86__rule
desc  ACTION //media/mojo/mojom:mojom__generator(//build/toolchain/gtos:clang_x86)
outs  170 项：34 个 IDL × {-forward.h, -import-headers.h, -test-utils.h, .mojom.cc, .mojom.h}
```

下列生成器参数取自图，保留原值；入口将图中外层 helper 替换为固定 Python，子解释器旗标
另列（`-B -S`，**不带** `-I`，因为生成脚本从自己的目录 import 兄弟包 `generators` 与
`checks`，`-I` 会把脚本目录从 `sys.path` 移除）：

```
../source/mojo/public/tools/bindings/mojom_bindings_generator.py
  --use_bundled_pylibs -o gen generate -d ../source/ -I ../source/
  --bytecode_path gen/mojo/public/tools/bindings
  --filelist=gen/media/mojo/mojom/mojom__generator.rsp
  -g c++ --typemap gen/media/mojo/mojom/mojom__type_mappings
```

## 3. 真实执行结果

`produce-run-02`（clang **未启动**，只跑 Python 生成器）：

| 项 | 值 |
|---|---|
| 动作 | `//media/mojo/mojom:mojom__generator` |
| 子进程 rc | 0 |
| 动作耗时 | 16.264 秒 |
| 入口总耗时 | 24.780 秒 |
| 日志 | 0 B |
| 暂存输入 / 声明输出 / 实测 | 37 / 170 / 207 |
| 实际集合门 | 通过（恰为二者并集，无多余、无缺失、无空文件） |
| Landlock | ABI 7，无 ledger 规则、无目录 EXECUTE、无 MAKE_SYM |
| 正常完成路径的已记录子进程回收 | 0（已测量；不代表异常退出链也已验收，见第5节） |

目标头（`gen/media/mojo/mojom/media_service.mojom-forward.h`）582 B，
`7380c232869f1c8d79772d6cada57309ef6920cd611d19ad9f64ae3606dcb1eb`，内容为真实的
`namespace media::mojom { class MediaService; }` 前置声明。

摘要时点：上表在生产动作完成后测得并写入 `produce-run-02` 回执；随后另一次 M 运行按固定
回执对 170 项副本逐项做先验与后验比对。这是跨生成/M运行的受管文件绑定，不证明 clang
逐个读取了全部 170 项；目标头已解析的结论来自新前沿与有序 include 链。

`produce-run-01` 是失败尝试，照实占次：生成器已真实启动（`child_pid` 非空、
暂存37项），但该模式传入 `wait_limit=None`，监管函数在
`deadline = start + wait_limit` 上抛 `TypeError`。业务worker约3.491秒后失败，
父入口约5.421秒后结束；未取得生成动作退出码。该目录随后仍有产物写出，不能声称异常时
生成器已随父入口停止。这些产物不获资格，成功事实只引用 `produce-run-02`。

## 4. 接入与真实 M

### 4.1 计划构造函数：一个 `-I`，其余 322 元素不动

基线是 015 M 的回执字段 `plan.argv`（**322** 项）与 `plan.cwd`。本轮只加一个前缀：

```
storage_root = OUT/overlay          # 文件按逻辑名落盘，逻辑名保留 gen/
include_root = storage_root/gen     # -I 指向这里
spelling     = 逻辑名去掉一个 gen/   # 编译器拼写
argv         = 322 + [-I<include_root>]，插在 015 前缀之后、-I../source 之前
```

插入点、crash 目录 token、`-MF` 值都按 token 定位；差异按**基线坐标**计算，插入不移动它。
M 回执实测：`argv_count=323`、`inserted_at=117`、`delta=[204,320]`、前缀序
`112,113,114,115,116,117,118,119`。

### 4.2 一个生产计划门，预检与 spawn 前共用

`gates.require_execution_plan` 不信计划里缓存的 `argv`/`count`/`delta`，而是从**实际
argv** 重新求插入点与差异、从 argv 取出 include 根，再逐成员确认在该根下解析且摘要等于
回执记录。父 `entry` 经 `spawn_if_allowed` 放行业务worker；实际生成器/clang的
`Popen` 位于worker内，是另一层启动点，M分支另检查 `plan_gate`。E1案例只覆盖父入口
的释放函数，不能称为整套执行器唯一的 `Popen` 路径。本轮实测170/170成员在该根下解析。

### 4.3 真实 M

`m-run-02`：clang 真正启动 1 次，rc1，132.242 秒；业务子进程 150.660 秒；父入口总
167.677 秒；日志 497 B、1 个 error。首错：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:60:10: fatal error:
  'media/mojo/mojom/remoting.mojom-forward.h' file not found
```

**前沿推进一格**：第59行所需的 `media_service.mojom-forward.h` 已解析；这不证明
整个media_service目标编译完成。下一个缺失头属于
`//media/mojo/mojom:remoting__generator` 这个独立target。
`m-run-01` 因业务隔离内缺clang EXECUTE而报 `PermissionError`，clang未启动；
约19.1秒是worker耗时，父入口36.152秒，照实占次。

## 5. 入口与外部依赖（不声称仅靠仓库可重现）

执行入口不在仓库内，位于证据根
`F:/GTOS-Chromium/artifacts/native-media-service-mojom-frontier-20261019-a`。

| 文件 | 用途 |
|---|---|
| `entry-016-v2.py` / `worker-016-v2.py` | produce-run-02实际使用的父入口/worker；以该OUT的source-snapshot绑定 |
| `entry-016-v6.py` / `worker-016-v6.py` | m-run-02实际使用的父入口/worker；以该OUT的source-snapshot绑定 |
| `entry-016-v8.py` / `worker-016-v8.py` | 后续案例与probe版本；没有用此版本重新运行生成器或M |
| `plan-016-v3.py` | 唯一的计划构造函数：存储根、拼写、`-I` 根、323 元素 argv、差异 |
| `gates-016-v2.py` | 共用判定：一个执行计划门、集合门、回执门、预算门、回收门、清理门、clang/action 门、账本门、事后复核、`decide`/`finalise` |
| `budget-016.py` | 账本：cheap 20×120 秒、prepare 6×600 秒、produce 4×1200 秒、content 3×600 秒，其余 0，累计 7200 秒 |
| `run-016-cases-v3.py` | 47项案例记录通过；其中清理窗例只调判定函数，P4实际走退出前超限分支，不能证明对应实际回收/退出后分支；runner内部加载entry-v7，不是外层entry-v8 |

固定外部依赖：`/usr/bin/python3` `298a9e83…`；clang
`toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang` `656e4031…`；ELF 解释器
`/lib64/ld-linux-x86-64.so.2` → `/usr/lib/x86_64-linux-gnu/ld-2.31.so` `cfd44e43…`。
三者都在业务执行**之前**解析并摘要，末尾对照同一固定值。

执行器的验收边界：正常 `produce-run-02` 与 `m-run-02` 的产物、日志及所记录回收
结果保留。异常路径仍有缺口：父入口没有设置subreaper，而内部动作另建session；worker
监督异常时没有覆盖内部动作的finally回收。父入口的残留0不能据此覆盖失去父进程的独立组。

清理窗目前在spawn前计算，未在每次实际清理前重新按同一绝对截止时间裁剪；最终时间样本
又早于postcheck（M约151.304秒取样，入口最终167.677秒）。本轮实际耗时未超上限，
但不能声称完整截止链已验证。R2只能算判定函数层通过。

解释器/clang/loader的执行前后固定摘要绑定与Python模块导入是两件事：当前动态加载函数
先执行模块，未先按独立期望摘要验字节。后续复用须先修该导入顺序及上述异常回收/截止链，
并验证实际调用路径；不为修正这些说明重跑本轮已成功的生成器或M。

## 6. 未运行项

以下全部 **NOT_RUN**，不得从本轮结果推断：

- `remoting` 及其后是否还有别的缺失依赖；
- Content CXX 编译、链接、`content_shell` 可执行文件、原生浏览器启动；
- socket、传输、DNS、接口名服务、内核网络线格式、地址转换函数行为；
- GN/Ninja 再生成、旧 mojom 全链重跑、整个 `media`/`blink`/`content` 构建；
- 内核/ISO/guest 重建、显示 VM 变更、新平台 ABI、新目标模式。
