# 原生 Content origin trials 产物前沿：`origin_trial_feature.mojom-forward.h` 的归属、产出与下一个首错

本文件记录 014 r2 结尾那个缺失头的归属判定、它的原始 action 的真实执行结果，以及
015 那一轮**第一次真正让 clang 读到这个头**的结果。目标头
`third_party/blink/public/mojom/origin_trials/origin_trial_feature.mojom-forward.h`
**不是源码树自有头**，而是 GN 生成动作
`//third_party/blink/public/mojom/origin_trials:origin_trial_feature__generator` 的输出：
它的输入IDL在历史步骤中由 `runtime_enabled_features.json5` 与
`templates/origin_trial_feature.mojom.tmpl` 生成，源树没有对应的自有`.mojom`文件；
本轮只执行消费该生成IDL/module的normal生成动作。该动作的5个声明产物在已验收视图里全部缺失；
produce-run-02内该动作成功一次，5项输出全部取得。

**范围与层级**：CXX 编译、链接、原生浏览器**均未运行**；socket、传输、DNS、接口名服务、
内核网络线格式与转换函数行为**均未扩展**。本轮 `-M` 真正跑起来（clang rc1、130.083秒，
入口总时长140.253秒、日志512 B），取得了新首错：所需目标头已解析，下一缺失头换成了另一组mojom
（`media/mojo/mojom/media_service.mojom-forward.h`）。这不说明 Content 能编译、能链接或能运行。

## 1. 前沿是什么，以及它属于哪一类

014 r2 结束时的真实首错（`native-runtime-feature-state-frontier-20261019-a/revision-r2/m-run-01`，
clang rc1，130.24 秒，日志 1348 B）：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h
→ … → third_party/blink/public/common/origin_trials/origin_trials.h:14:10: fatal error:
  'third_party/blink/public/mojom/origin_trials/origin_trial_feature.mojom-forward.h' file not found
```

包含路径本身不区分源码头与生成头，因此三个竞争解释逐项核对：

| 解释 | 结论 | 决定性依据 |
|---|---|---|
| A 源码树自有头，只是没被视图收录 | **不成立** | 真实 GN 图 `toolchain.ninja` 第 21556 行是 `rule __third_party_blink_public_mojom_origin_trials_origin_trial_feature__generator___build_toolchain_gtos_clang_x86__rule`，第 21561 行该 `build` 语句把这条路径列为**输出**；固定 source 视图 `native-content-mojom-core-consumer-20261019-a/run-10/source` 的 `third_party/blink/public/mojom/origin_trials/` 下只有 `BUILD.gn`、`OWNERS`、`origin_trial_state_host.mojom`、`origin_trials_settings.mojom` 四个文件，**没有** `origin_trial_feature.mojom` 源文件 |
| B 是生成动作的输出，动作从未运行/从未接入 | **成立** | 该 action 的5个声明输出（`-forward.h`、`-import-headers.h`、`-test-utils.h`、`.mojom.cc`、`.mojom.h`）在已验收 gen 视图里**全部缺失**；同时它的**输入**齐备（`origin_trial_feature__generator.rsp`、`origin_trial_feature.mojom`、`.mojom-module`、`__type_mappings`、`cpp_templates.zip`）。缺的是产物，不是输入 |
| C 源/图/合同版本不一致 | **未全面排除** | 目标头属于真实图中的 generator 输出，输入文件存在，足以定位本次缺失产物；但 json5、生成脚本及 blink 源树未逐项绑定到完整历史版本合同，不能据此证明全部源/图版本一致 |

解释 A 的依据必须写准：判断「是不是源码头」靠的是 GN 图里这条路径是某条 `build` 语句的
输出，加上固定 source 视图里不存在它。本仓库的 `docs/` 目录只是文档目录，它有没有同名
文件与这个判断**无关**。

**同一目录下的 `origin_trials_settings.mojom-*` 不能被当作缺失的那个头**：它是
`origin_trials_settings` 这个**另一个** target 的产物（`-forward.h` 899 B 等），文件存在，
但内容与 `origin_trial_feature` 无关；把它复制过去会得到「文件存在但声明不对」的假进展。

本轮依据解释B补齐产物，未进行源码视图或版本修复；生成与真实M证据不等于全部来源版本已完成资格验证。

## 2. owner、动作范围与原始 argv

动作来自固定 GN 图 `native-content-frontier-20261018-a/gn-clipboard-01/build/toolchain.ninja`
（45456305 B，`a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175`）。
下列生成器参数取自图，保留原值；入口将图中外层helper替换为固定Python，子解释器旗标另列：

```
../source/mojo/public/tools/bindings/mojom_bindings_generator.py
  --use_bundled_pylibs -o gen generate -d ../source/ -I ../source/
  --bytecode_path gen/mojo/public/tools/bindings
  --export_attribute BLINK_COMMON_EXPORT
  --export_header third_party/blink/public/common/common_export.h
  --filelist=gen/third_party/blink/public/mojom/origin_trials/origin_trial_feature__generator.rsp
  -g c++ --typemap gen/third_party/blink/public/mojom/origin_trials/origin_trial_feature__type_mappings
```

子解释器旗标是 `-B -S`，**不带** `-I`：生成脚本从自己的目录 import 兄弟包
`generators` 与 `checks`，而 `-I` 会把脚本目录从 `sys.path` 移除，导致
`ModuleNotFoundError`（这是 013 的 v1 真实踩过的失败，v2 起改用脚本目录直接在
`sys.path[0]` 的方式）。

## 3. 真实执行结果

`produce-run-02`（`produce-015-v2.py`，clang **未启动**，只跑 Python 生成器）：

| 项 | 值 |
|---|---|
| 动作 | `origin_trial_feature__generator` |
| 子进程 rc | 0 |
| 子进程耗时 | 4.713 秒 |
| 入口总耗时 | 6.052 秒 |
| 日志 | `action.log` 0 B |
| 声明产物 / 实得 | 5 / 5 |
| 实际集合门 | 通过（staged 5 + 声明 5 = 10 项，无多余、无缺失、无空文件） |
| 残留后代 | 0（已测量） |
| Landlock ABI | 7 |

5 项输出（`gen/` 前缀，字节与摘要）：

| 产物 | 字节 | SHA256 |
|---|---|---|
| `origin_trial_feature.mojom-forward.h` | 690 | `4df60e0edee55fbf2989b636e3125b95c7f4d9ebf37ea695ac7f8cc872b44de8` |
| `origin_trial_feature.mojom-import-headers.h` | 596 | `6b64eaf063182095456bec8a1eb0a578a0ced6a3ed6272e2387ed5ee75378322` |
| `origin_trial_feature.mojom-test-utils.h` | 775 | `2a160cc90fef50c2b9775d5e06429dd279312009cb683f79637ee3eb049d2de1` |
| `origin_trial_feature.mojom.cc` | 283 | `556fe113879bcfc808e2de2a3bd33c6a4cc5be97dab8eebe57875301593f9c67` |
| `origin_trial_feature.mojom.h` | 1316 | `9784f555b4eec0eb95ceb4a4d65918f9b4da27738ecb44850b3716158790ef79` |

摘要时点：上表在生产动作完成后测得并写入produce-run-02回执；随后另一次M运行按固定回执
对5项副本逐项做先验与后验比对。这是跨生成/M运行的受管文件绑定，不证明clang逐个读取了
全部5项；目标头已解析的结论来自新前沿与有序include链。未逐项重验全部历史source内容。

`produce-run-01` 是同一入口的失败尝试（`PermissionError: [Errno 13] Permission denied:
'/usr/bin/python3'`，1.211 秒，clang 未启动）：Landlock 规则给了 `/usr/bin` 只读，却只给
`/usr/bin/python3` 本体 EXECUTE，**没有**给 ELF 解释器 `/lib64/ld-linux-x86-64.so.2`
EXECUTE，`execve` 因此 EACCES。v2 补上解释器的 EXECUTE 后成功。这是本轮唯一一次
未计划的沙箱接线失败，记为`produce-run-01`，不是预设负例，不重跑。

## 4. 接入与真实 M：已取得新首错

### 4.1 计划构造函数：一个 `-I`，其余 321 元素不动

基线是 014 r2 的 M 回执 `revision-r2/m-run-01/status.json`，字段 `plan.argv`（**321** 项）
与 `plan.cwd`（`…/native-content-mojom-core-consumer-20261019-a/run-10/build`）。本轮
只加一个前缀：

```
storage_root = OUT/overlay            # 文件按逻辑名落盘，逻辑名保留 gen/
include_root = storage_root/gen       # -I 指向这里
spelling     = 逻辑名去掉一个 gen/     # 编译器拼写
argv         = 321 + [-I<include_root>]，插在 014 前缀之后、-I../source 之前
```

插入点、crash 目录 token、`-MF` 值都按 token 定位；差异列表按**基线坐标**计算，所以插入
本身不会让差异位移。M 回执实测：`argv_count=322`、`inserted_at=116`、
`argv_delta=[203,319]`，与基线里 crash token / `-MF` 值的下标一致。

前缀顺序（M 回执 `verdicts.order.indices` = `112,113,114,115,116,117,118`）：

```
012 SDK → 012 storage overlay → 013 250 → 014 9 → 本轮 5 → -I../source → -Igen
```

### 4.2 一个生产计划门，预检与 spawn 前共用

014 r2 的判定里 `verify_plan` 的结果**算出来了但没驱动 spawn**：M 分支在 `Popen` 之前只
看预算。015 把这件事收成一条：`gates.require_execution_plan` 不信计划里缓存的
`argv`/`count`/`delta`，而是从**实际 argv** 重新求插入点与差异、从 argv 里取出 include 根，
再逐项确认每个成员在该根下解析且摘要等于回执记录；`entry` 的 `spawn_if_allowed` 是唯一
调用 `Popen` 的地方，门不是**测得的** `True` 就返回 `None`。同一个 `plan_gate` 记录进入
最终判定，后续步骤不能把它翻成通过。

廉价例直接数 `Popen` 调用次数（`SpawnSpy` 一被调用即失败），7 个假计划各自走一遍生产
释放路径，全部 0 次：缺判定、`ok` 不是 bool、解析失败、多一个 `-MG`、include 根抬高一级、
成员摘要被改、计划与自己声明的根不一致。

### 4.3 入口三处硬伤

- **回收**：`run_child` 把 spawn、监管、首次回收放进同一个 `try`，`finally` 里无条件对
  「本次启动的子进程」再做一次有界回收（含组杀）。廉价例 X1/X2 用会抛异常的监管桩证明：
  异常原样抛出，子进程仍被回收，`remaining=0`、`measured=True`。
- **事后复核**：结束前检查解释器、clang、入口/判定/计划/账本四模块、321基线回执、
  本轮生产回执与GN图的固定摘要；012的4项、013的250项、014的9项按既有记录复读，
  本轮5项还对照生产回执与计划记录。loader虽列入检查表，其期望摘要却在末尾才取，
  不能证明前后未变；完整历史source/gen也不在本次内容后验范围内。
- **日志大小**：先按 poll 判一次，**子进程退出后**再判一次，所以「poll 先看到退出」不会
  掩盖超限日志（廉价例P4覆盖子进程已退出的情形）。

finally的cleanup结果当前只记录，未合入最终decide；其固定30秒清理窗也未再次按入口剩余
时间裁剪。当前M两次回收均实测0，但失败/超限路径还须在下次复用前补齐，不能宣称整套执行器
所有异常情况均已验收。X例实际启动短暂sleep子进程并注入监督异常；它不是纯假子进程例。

### 4.4 真实 M

`m-run-01`：clang真正启动，rc1，130.083秒；入口总时长140.253秒，日志512 B、1个error。
当前实际执行所判各门通过，判定码0；不等于上一节未覆盖的异常路径已通过。首错：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:59:10: fatal error:
  'media/mojo/mojom/media_service.mojom-forward.h' file not found
```

**前沿推进一格**：`origin_trials.h:14` 那一行不再报错，目标头被读到；下一个缺失头是
`media` 组。M 回执里 `postcheck.ok=true`、`reap.remaining=0`、`cleanup.remaining=0`、
`write_probe.refused=true`（探针在本 run 输出之外，且被拒），`dep_present=false`。

## 5. 入口与外部依赖（不声称仅靠仓库可重现）

执行入口不在仓库内，位于证据根
`F:/GTOS-Chromium/artifacts/native-origin-trials-mojom-frontier-20261019-a`。

| 文件 | 用途 |
|---|---|
| `produce-015-v2.py` | 本轮生产入口：原始 action、Landlock、deadline、回收（已执行，`produce-run-02`）；`produce-015.py` 是同入口的 v1，已执行并冻结（`produce-run-01`） |
| `plan-015.py` | **唯一的计划构造函数**：存储根、编译器拼写、`-I` 根、322 元素 argv、差异，一次决定 |
| `gates-015.py` | 共用判定函数：一个执行计划门、集合门、回执门、预算门、回收门、clang 门、事后复核、`decide` 唯一出口 |
| `entry-015.py` | 唯一入口，两个模式：`preflight`（不启动 clang）与 `M` |
| `run-015-cases.py` / `-v2.py` / `-v3.py` | 廉价入口，各 71 例；v1 在 `cases-run-01` 失败（Y6 暴露事后复核只看回执不看计划），v2/v3 通过 |
| `budget-015.py` | 账本类别上限：cheap 16×120秒、prepare 6×600秒、produce 4×1200秒、content 3×600秒，其余类别0，累计7200秒；本次producer自身另设1100秒 |
| `check-syntax-015.py` | 静态检查（内存编译 + 跨文件摘要常量一致性），不 claim、不写产物 |

案例版本边界：cases-run-03绑定的四模块摘要与当前M一致；前两次案例加载过的entry/plan
后来在原路径修改，且没有对应source-snapshot，不能由当前文件复现那两次旧字节。当前M及
两个producer的已保存快照独立保留。辅助syntax脚本的外部执行未计入原7对账本，不能把静态
检查自行定义为免费重试；其纪律差异另记于审核，不为补说明重跑。

固定外部依赖：`/usr/bin/python3` `298a9e83…`；clang
`toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang` `656e4031…`。这两个摘要在入口里
被核对，变化即拒绝运行。

## 6. 未运行项

以下全部 **NOT_RUN**，不得从本轮结果推断：

- `media` 组及其之后是否还有别的缺失依赖；
- Content CXX 编译、链接、`content_shell` 可执行文件、原生浏览器启动；
- socket、传输、DNS、接口名服务、内核网络线格式、地址转换函数行为；
- GN/Ninja 再生成、旧 mojom 全链重跑、整个 `third_party/blink` 构建；
- 内核/ISO/guest 重建、显示 VM 变更、新平台 ABI、新目标模式。
