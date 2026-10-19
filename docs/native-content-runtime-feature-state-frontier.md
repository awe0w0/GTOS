# 原生 Content runtime feature state 产物前沿：`runtime_feature_state_context.h` 的归属、产出与首次真实读取

本文件记录 013 结尾那个缺失头的归属判定、它的两个原始 action 的真实执行结果，以及
014 r2 那一轮**第一次真正让 clang 读到这个头**的结果。目标头
`third_party/blink/public/common/runtime_feature_state/runtime_feature_state_context.h`
**不是源码树自有头**，而是 GN 生成动作
`//third_party/blink/public/common:make_runtime_feature_state_contexts_headers` 的输出；
它自身 include 需要的 `runtime_feature.mojom-shared.h` 来自另一个动作
`//third_party/blink/public/mojom/runtime_feature_state:runtime_feature_state_shared__generator`。
这两个动作的9个声明产物在已验收视图里全部缺失。produce-run-02内两动作各成功一次，
9项输出全部取得；另有produce-run-01中的shared失败尝试，因此是2个不同动作、3次实际尝试。

**范围与层级**：CXX 编译、链接、原生浏览器**均未运行**；socket、传输、DNS、接口名服务、
内核网络线格式与转换函数行为**均未扩展**。014 那轮三次 Content `-M` 入口尝试**全部在
clang 启动前失败**，因此那轮没有新首错。014 r2 修好入口接线后，`-M` 真正跑起来
（clang rc1、130.24 秒），取得了新首错：目标头**已被读到**，下一个缺失头换成了另一组
mojom（`origin_trials`）。这不说明 Content 能编译、能链接或能运行。

## 1. 前沿是什么，以及它属于哪一类

013 结束时的真实首错（`native-network-mojom-forward-20261019-a/m-run-02`，clang rc1，
143.199 秒，日志 807 B）：

```
content/shell/app/shell_main_delegate.cc:36
→ … → content/public/browser/navigation_handle.h:41:10: fatal error:
  'third_party/blink/public/common/runtime_feature_state/runtime_feature_state_context.h' file not found
```

013 当时把这个头的归属留作未决：包含路径本身不区分源码头与生成头。三个竞争解释，按已有证据逐项核对：

| 解释 | 结论 | 决定性依据 |
|---|---|---|
| A 源码树自有头，只是没被视图收录 | **不成立** | 真实 GN 图里该路径是一条 `build` 语句的**输出**（`toolchain.ninja` 的 headers rule），不是任何 source 目标；在固定 source 视图 `native-content-mojom-core-consumer-20261019-a/run-10/source` 里该路径不存在 |
| B 是生成动作的输出，动作从未运行/从未接入 | **成立** | 该 action 的 2 个声明输出，加上 shared action 的 7 个，共 **9 个**在已验收 gen 视图中**全部缺失**（见 §2 表）；同时这两个 action 的**输入**都在已验收视图里齐备（rsp、`.mojom`、`-module`、`build_metadata`、`cpp_templates.zip`）。缺的是产物，不是输入 |
| C 源/图/合同版本不一致 | **未全面排除** | 目标头属于真实图中的 headers 输出，输入文件存在，足以定位本次缺失产物；但json5、生成脚本及runtime源树未逐项绑定到完整历史版本合同，不能据此证明全部源/图版本一致 |

解释 A 的依据必须写准：判断「是不是源码头」靠的是 GN 图里这条路径是某条 `build` 语句的
输出，加上固定 source 视图里不存在它。本仓库的 `docs/` 目录只是文档目录，它有没有同名
文件与这个判断**无关**，不能拿来当依据。

本轮依据解释B补齐产物，未进行源码视图或版本修复；生成与真实M证据不等于全部来源版本已完成资格验证。

## 2. owner、动作范围与原始 argv

action 身份取自真实 GN 包 `native-content-frontier-20261018-a/gn-clipboard-01/build` 的
`toolchain.ninja`：

| 项 | headers action | shared action |
|---|---|---|
| rule 名 | `__third_party_blink_public_common_make_runtime_feature_state_contexts_headers___build_toolchain_gtos_clang_x86__rule` | `__third_party_blink_public_mojom_runtime_feature_state_runtime_feature_state_shared__generator___build_toolchain_gtos_clang_x86__rule` |
| description | `ACTION //third_party/blink/public/common:make_runtime_feature_state_contexts_headers(//build/toolchain/gtos:clang_x86)` | `ACTION //third_party/blink/public/mojom/runtime_feature_state:runtime_feature_state_shared__generator(//build/toolchain/gtos:clang_x86)` |
| 声明输出数 | **2**（有序：`runtime_feature_state_context.h`、`runtime_feature_state_read_context.h`） | **7**（`-data-view.h`、`-features.h`、`-params-data.h`、`-send-validation.h`、`-shared-internal.h`、`-shared.cc`、`-shared.h`） |
| 目标头成员关系 | 是，第 1 个输出 | 否（但它是目标头 include 闭包的必要成员） |
| 在已验收视图中是否存在 | **0/2** | **0/7** |

已验收视图（`native-content-mojom-core-consumer-20261019-a/run-10/build`）里
`gen/third_party/blink/public/mojom/runtime_feature_state/` 只有输入侧文件
（`runtime_feature.mojom`、`runtime_feature.mojom-module`、`runtime_feature_state.build_metadata`、
两个 `__generator.rsp`、typemap 等），**没有任何一个** `.h`/`.cc` 产物；
`gen/third_party/blink/public/common/runtime_feature_state/` 这个目录**根本不存在**。
这是「产物缺失、输入齐备」的直接观测，不是从首错反推。

两个 action 的原始命令（`toolchain.ninja` 原文，均由 `content-helper-clipboard-v1.py` 包一层）：

```
../source/third_party/blink/renderer/build/scripts/run_with_pythonpath.py \
  -I ../source/third_party/blink/renderer/build/scripts -I ../source/third_party \
  -I ../source/third_party/pyjson5/src -I ../source/tools \
  ../source/third_party/blink/renderer/build/scripts/make_runtime_feature_state_contexts_headers.py \
  ../source/third_party/blink/renderer/platform/runtime_enabled_features.json5 \
  --output_dir gen/third_party/blink/public/common/runtime_feature_state/
```

```
../source/mojo/public/tools/bindings/mojom_bindings_generator.py --use_bundled_pylibs \
  -o gen generate -d ../source/ -I ../source/ --bytecode_path gen/mojo/public/tools/bindings \
  --filelist=gen/third_party/blink/public/mojom/runtime_feature_state/runtime_feature_state_shared__generator.rsp \
  --generate_non_variant_code -g c++ \
  --export_attribute COMPONENT_EXPORT\(MOJOM_SHARED_BLINK_COMMON_EXPORT\) \
  --export_header base/component_export.h
```

## 3. 真实执行结果

两个 action 在同一次受控执行（`produce-run-02`，14.928 秒）内完成：shared 4.669 秒、
headers 8.828 秒，两者子进程 rc 均为 0，日志 0 B，后代残留 0。隔离与边界：
Landlock ABI 7、user namespace 映射为 root、network namespace 仅 `lo` 且无路由、
subreaper 回读 1。集合门：声明 9 + 暂存输入 4 = 期望 13，实际 13，
`missing`/`extra`/`empty`/`non_regular` 四项全空，判定码 0。

| 产物（gen 相对） | 字节 | SHA256 |
|---|---:|---|
| `…/common/runtime_feature_state/runtime_feature_state_context.h` | 3205 | `c0036c07e851c54792ed8a1d26d8080d08c81c0051fc138ff019cea4ea0e1cc0` |
| `…/common/runtime_feature_state/runtime_feature_state_read_context.h` | 7928 | `689db66316a2fe26d16f298ba7df1d66550a424d984ea6c6f642eaad59893efe` |
| `…/mojom/runtime_feature_state/runtime_feature.mojom-shared.h` | 3242 | `618e101da62fc12023f73c538e234ad3acef8c99468579d560f100dfb12398a2` |
| `…/runtime_feature.mojom-shared.cc` | 2572 | `98e99949d5659abd8fe8bcef3ee64144506caa30e73122a33bac2092389372f0` |
| `…/runtime_feature.mojom-shared-internal.h` | 1712 | `083305b8ae7c85b97f2cb0b03eede346cc3c96100bcb0be188cafc34957d9007` |
| `…/runtime_feature.mojom-send-validation.h` | 1218 | `944d6e829d43eacd46b77dba9735343b36eb3afbe330bde8f92921bac6391e49` |
| `…/runtime_feature.mojom-data-view.h` | 1997 | `b35821911472d392556e5b4e805932ad578b4fceb4b52e56dc8b44dc9ab7b120` |
| `…/runtime_feature.mojom-features.h` | 586 | `5d67b941821cdfc679a7e8dc2308ae7b5e948b46accd3760cba404fc6246d3f7` |
| `…/runtime_feature.mojom-params-data.h` | 925 | `f07cca3537d70b6f36589f94671dc392db5b4d46b466ab8c350f07d2a716672c` |

**9 个产物与 4 个暂存输入是两件事。** 上表 9 个是 action 的真实输出；另有 4 个文件
（`runtime_feature_state_shared__generator.rsp` `51b65b46…`、`runtime_feature.mojom` `cb0cace5…`、
`runtime_feature.mojom-module` `e8757d3d…`、`cpp_templates.zip` `4763b192…`）是从已验收视图
**逐字节复制**进来的输入，不是本轮新增输出；其中若干文件本身来自历史生成动作。生产入口把它们与产物
一起按「9 声明 + 4 暂存 = 13」做集合核对，所以 13/13 这个数字里只有 9 个是产出。

**摘要的时点。** produce-run-02在两个动作完成后计算落盘输出摘要并写入receipt，不是动作
运行期间另存了一份摘要再与之比较。前一轮审核另行重读了当前9项输出和4项暂存文件；r2消费
前后又将新前缀对固定receipt复核。这些都不代表所有源文件、模板或`.mojom`在生成前已绑定
完整历史摘要。旧producer核对了解释器，复制模板并记录当前摘要，没有实现模板变化即拒绝，
也没有执行clang。

**include 闭包的正确读法。** 9 个产物中共 **35 条直接引号 include**，在一个独立进程内按
某一搜索顺序解析，35/35 命中、0 遮蔽（`closure-run-02`）。这只说明这 35 个名字在**那一种**
搜索顺序下能命中。它不是递归闭包（没有展开被命中文件的 include），不是 clang 的行为，
**也不是 M 接线的证明**——r1审核发现未执行的v4入口给出的`-I`根少一层`gen`，而该checker
另用正确根查找；旧三次M尝试均在clang启动前退出，不能说它们已实测这个搜索根反例。
另有一次 `closure-run-01` 报 22 条未解析，其结论已整体撤回
（overlay 根取高了一层、`<...>` 形式被当成引号 include 解析），不作为任何依据。

## 4. 接入与真实 M：已取得新首错

按审核要求分层验收，四层现在都成立，但**第四层的结果是「读到了目标头，卡在下一个头」**：

| 层级 | 状态 | 依据 |
|---|---|---|
| 目标头真实来源/输出 | **成立** | §2、§3 |
| prefix 包含且字节绑定 | **成立** | 前缀成员摘要取自 `produce-run-02` 生成时的记录，生成目录与消费 overlay 两侧分别核对；前缀不含已验收视图中任何同名文件 |
| M 是否真正读到该头 | **成立** | `m-run-01` 的 clang include 栈逐层写出了 `overlay/gen/.../runtime_feature_state_context.h` 与 `..._read_context.h` 的绝对路径 |
| M rc / 新首错 | **成立** | clang rc1、130.24 秒、日志 1348 B、1 条错误，新首错为 `origin_trials` 组的 mojom 前置头 |

### 4.1 014 那轮三次尝试（与本节结论无关，单独保留）

014 的三次 `-M` 入口尝试**全部在 clang 启动前失败**，不是编译结果：

| 尝试 | 秒 | 停在哪一步 | 直接原因 |
|---|---:|---|---|
| `m-run-01` | 1.154 | 回执绑定门 | 前缀成员名上又加了一层 `gen/`，与实际回执键形状不符（`gen/gen/…` vs `gen/…`） |
| `m-run-02` | 1.673 | argv 差异门 | 允许变更的两个下标取自插入**后**的 argv，而差异列表按基线坐标报告 |
| `m-run-03` | 1.676 | argv 差异门 | 同上；`-MF` 的**值**在标志后一位，基线下标是 318 而非 317 |

三次都停在同一个根因族：**对固定基线做下标运算**。014 那轮据此写了 `-v4` 入口和
6 个函数级小例（I1–I6，`checks-run-05`），但**该入口从未执行**（content 预算 3/3）。
I1–I6 是函数级小例，不能当作 M 结果。

### 4.2 r2：一个计划构造函数，预检与 M 共用

r1 复核指出的决定性缺陷是接线：入口把成员复制到 `overlay/<logical>`（`logical` 本身已带
`gen/`），命令行却给 `-I<overlay>`，编译器于是少找一层 `gen`。修法不是再补一次下标，而是
把「存到哪、编译器怎么拼、`-I` 指向哪」收进**一个**函数，预检与 M 都只消费它的返回值：

```
storage_root = OUT/overlay
storage file = storage_root/logical            （logical 保留 gen/ 前缀）
include_root = storage_root/gen
spelling     = logical 去掉一个前缀 gen/
include_root/spelling 与 storage_root/logical 是同一个文件
```

于是三件事不可能再各说各话。这个构造函数的 11 个廉价例（N1–N11）用**真实数据**跑：
真实 320 元素基线、真实 9 成员回执、真实 012/013 前缀。

**预检（`preflight-01`，5.626 秒，判定码 0，`clang_started=false`）**：八项门全过。

| 门 | 结果 |
|---|---|
| 成员/真实集合/回执绑定 | 9 个成员、磁盘上枚举出的集合与声明完全一致、每个成员与产出时的摘要一致 |
| 按真实 `-I` 根解析 | **9/9** 在 `storage_root/gen` 下用编译器拼写命中，且字节等于回执记录 |
| 前缀顺序 | `-I<012 sdk>`(112) → `-I<012 overlay>`(113) → `-I<013 250>`(114) → `-I<新根>`(115) → `-I../source`(116) → `-Igen`(117) |
| argv 差异 | 321 = 320 + 1，差异**恰好**是 crash 目录 token 与 `-MF` 的值（基线坐标 202、318） |
| 旧前缀字节 | 012 的 4 个、013 的 250 个，逐个与已验收记录一致 |
| 负控 | 把 `include_root` 抬高一层 → 9 个成员全部解析失败并被拒；多改一个编译开关 → 差异列表多一项并被拒；多插一个 `-MG` → 长度变化并被拒 |

**M（`m-run-01`，136.07 秒，判定码 0）**：clang 真正启动并完成，rc1，130.24 秒，
日志 1348 B，1 条错误，后代残留 0（实测），预算上限 540 秒，后置检查通过。
隔离：Landlock ABI 7、`no_new_privs`、`EXECUTE` 只给解释器/clang/loader 三个确切文件、
不给任何目录 `EXECUTE`、不给 `MAKE_SYM`、OUT 之外的写探针被拒（EACCES）。

该次后验覆盖新9项前缀的集合/摘要及入口、gates、plan、budget、基线与producer receipt；
未在末尾重新核对clang、解释器和旧012/013的254项前缀。当前正常运行的八项计划结果均为True，
但M放行尚未统一消费全部计划门，异常后也缺finally保证清理已启动的clang。它们须在后续复用
前补齐；本次成功不能作为整个执行器全部拒绝/异常路径已验收的证明，不为此重复本次M。

新首错（clang 原文，缩进为 include 栈）：

```
In file included from ../source/content/shell/app/shell_main_delegate.cc:36:
In file included from ../source/content/shell/browser/shell_content_browser_client.h:15:
In file included from ../source/content/public/browser/content_browser_client.h:51:
In file included from ../source/content/public/browser/process_selection_deferring_condition.h:12:
In file included from ../source/content/public/browser/navigation_handle.h:41:
In file included from <OUT>/overlay/gen/third_party/blink/public/common/runtime_feature_state/runtime_feature_state_context.h:17:
In file included from <OUT>/overlay/gen/third_party/blink/public/common/runtime_feature_state/runtime_feature_state_read_context.h:19:
../source/third_party/blink/public/common/origin_trials/origin_trials.h:14:10: fatal error:
  'third_party/blink/public/mojom/origin_trials/origin_trial_feature.mojom-forward.h' file not found
```

两点分开看，不要合并：

1. **产物头被读到了。** include 栈里的两条绝对路径都在本轮新增的 `-I<storage_root>/gen`
   之下，这正好是 r1 反例所说的那一层。旧写法 `-I<overlay>` 会在第 6 行就断掉。
2. **下一个缺失头是另一件事。** `origin_trial_feature.mojom-forward.h` 属于 `origin_trials`
   那组 mojom，不在这轮 9 个成员里，也不在 013 的 250 个正常前缀里。这是 013 那轮把
   目标头当作首错时**看不到**的依赖，因为链子在更早处就断了。本轮不声称它是唯一剩余依赖，
   也不声称补上它就能编译：没有跑，就没有结论。

### 4.3 预算门的真值

014 那轮的生产入口有 `budget_samples: False`，而且预算样本**不驱动** spawn 与退出——
也就是说「预算门失败会不会真的阻止启动」当时从未被验证过。r2 把这件事变成可证伪的：
`budget_gate` 失败时 `Popen` 不被调用，且预算失败有独立判定码 139。廉价例 B2（预算耗尽
→ 等待上限 0 → 拒绝）与 D5（COMPILE 记录里预算失败 → 139）各证一半，M 记录里
`budget_spawn`/`budget_final` 都保留原始单调值，四舍五入只用于显示。

## 5. 入口与外部依赖（不声称仅靠仓库可重现）

执行入口不在仓库内，位于证据根
`F:/GTOS-Chromium/artifacts/native-runtime-feature-state-frontier-20261019-a`。
014 的入口在包根，逐字节摘要见 `manifest-014.json`；r2 的入口在 `revision-r2/`，
摘要见 `manifest-014-r2.json`。

| 文件 | 用途 |
|---|---|
| `produce-014-v2.py` / `run-014-produce-v2.sh` | 014 生产入口：两次原始 action、Landlock、deadline、回收（已执行，`produce-run-02`） |
| `gates-014.py` → `gates-014-v4.py` | 014 共用判定函数；v4在checks-run-05中被测，完整M v4入口未执行；历史字节冻结 |
| `run-014-checks.py` → `run-014-checks-v5.py` | 014 廉价测试入口（49 例，`checks-run-05`） |
| `budget-014.py` | 014 启动前 claim/close 账本（已冻结） |
| `m-compile-014.py` → `-v4.py`、`run-014-m.sh` → `-v3.sh` | 014 的 Content `-M` 入口；v1–v3 已执行并冻结，v4 未执行 |
| `include-closure-014-v2.py` | 产物 include 闭包解析（`closure-run-02`） |
| `syntax-014.py` | 014 语法检查，内存编译、不写 bytecode |
| `revision-r2/plan-r2.py` | **r2 唯一的计划构造函数**：存储根、编译器拼写、`-I` 根、321 元素 argv、差异，一次决定 |
| `revision-r2/gates-r2.py` | r2 共用判定函数：集合门、回执门、预算门、回收门、clang 门、`decide` 唯一出口 |
| `revision-r2/entry-r2.py` | r2 唯一入口，两个模式：`preflight`（不启动 clang）与 `M` |
| `revision-r2/run-r2-cases.py` | r2 唯一廉价入口（51 例，`cases-run-03`） |
| `revision-r2/budget-r2.py` | r2 账本：cheap 8 × 120 秒、content 1 × 600 秒、受控总时长 1800 秒，其余类别为 0 |

固定外部依赖：`/usr/bin/python3` `298a9e83…`；clang
`toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang` `656e4031…`。这两个摘要在入口里
被核对，变化即拒绝运行。模板 `cpp_templates.zip` 的摘要被记录，但入口**没有**实现
「模板摘要变化即拒绝」——见 §3 的摘要时点说明。

## 6. 未运行项

以下全部 **NOT_RUN**，不得从本轮结果推断：

- `origin_trials` 组的 mojom 前置头产出，以及它之后是否还有别的缺失依赖；
- Content CXX 编译、链接、`content_shell` 可执行文件、原生浏览器启动；
- socket、传输、DNS、接口名服务、内核网络线格式、地址转换函数行为；
- GN/Ninja 再生成、旧 mojom 全链重跑、整个 `third_party/blink` 构建；
- 内核/ISO/guest 重建、显示 VM 变更、新平台 ABI、新目标模式。
