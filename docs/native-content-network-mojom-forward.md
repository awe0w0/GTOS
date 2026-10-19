# 原生 network mojom normal 产物前沿：`url_loader_base` 的 250 个 normal 输出

本文件记录 `//services/network/public/mojom:url_loader_base__generator` 这一个原始
**normal**（非 blink 变体）Mojo action 的真实执行结果：250 个声明产物已全部产生，目标头
`services/network/public/mojom/declarative_performance_observer.mojom-forward.h` 已取得，
并在一次真实 Content `-M` 中越过 012 的首错。

**范围**：CXX 编译、链接、原生浏览器**均未运行**；socket、传输、DNS、接口名服务、内核网络
线格式与转换函数行为**均未扩展**。生成成功只说明该 forward 头由它的真实 normal owner 产出
并且能在真实 include 链中被解析，不说明 Content 能编译、能链接或能运行。本文档中凡标
「r2 新增」的内容都是本轮新写的门与其夹具验证，**不是** g-run-04 / m-run-02 当时已经执行过
的东西。

## 1. 前沿是什么，以及它属于哪一类

012 结束时的真实首错（`native-address-sdk-20261019-a/m-run-03`，clang rc1，115.568 秒）：

```
content/shell/app/shell_main_delegate.cc:36
→ content/shell/browser/shell_content_browser_client.h:15
→ content/public/browser/content_browser_client.h:51
→ content/public/browser/process_selection_deferring_condition.h:12
→ content/public/browser/navigation_handle.h:38:10: fatal error:
  'services/network/public/mojom/declarative_performance_observer.mojom-forward.h' file not found
```

对这条前沿有三个竞争解释，本轮的答案不是推断而是逐条排除：

| 解释 | 结论 | 决定性依据 |
|---|---|---|
| 1 产物已存在，仅未接入视图 | **不成立** | 该 owner 的 normal 声明产物有 250 个；在这 250 个里，已验收 gen 视图此前**一个都没有**（`normal_same=0`）。已验收视图中确有六个同名 basename（`connection_allowlist`、`content_security_policy`、`integrity_algorithm`、`integrity_metadata`、`source_location`、`web_sandbox_flags`），但它们来自**其他 owner**，不属于这 250 个的成员 |
| 2 只产过 shared/parser/map，没有 normal | **成立** | 同一 owner 的 shared generator 已跑过（350 个输出），50 个 `-module` pickle 全部在场，但 normal action 从未完成 |
| 3 源/图/合同版本不一致 | **不成立** | 目标头确实在该 normal action 声明的 250 个 outputs 内；`.mojom` 源存在（2621 B）；50 个 module 与 1 个 typemap 在审核时与历史 U 回执逐项相符（见 §3）。注意：本轮 250 个输出全部是新增，因此「与旧产物逐字节等价」这件事本身不成立，不能用它去排除源/图版本差异；排除版本解释的是**声明成员关系 + 输入层级**，不是产物重叠 |

因此本轮做的是解释 2 的补齐，不是解释 3 的版本修复，也不是解释 1 的接线。

## 2. owner、动作范围与原始 argv

action 身份取自真实 GN 包 `native-content-frontier-20261018-a/gn-clipboard-01/build` 的
`toolchain.ninja`（45456305 B），不是重建的图：

| 项 | 值 |
|---|---|
| rule 名 | `__services_network_public_mojom_url_loader_base__generator___build_toolchain_gtos_clang_x86__rule` |
| description | `ACTION //services/network/public/mojom:url_loader_base__generator(//build/toolchain/gtos:clang_x86)` |
| 声明输出数 | **250**（有序，见 `manifest-013.json`） |
| 目标头成员关系 | 是，位于该 action 的 outputs 内 |
| 生成器变体 | normal，`-g c++`，无 blink |

原始命令（`rule-generator.txt`，676 B /
`d738eee6513add9bb3610277e509a320954a4ffe1289e1a7830a2c869040d381`）由
`content-helper-clipboard-v1.py` 包一层：

```
content-helper-clipboard-v1.py ../source/mojo/public/tools/bindings/mojom_bindings_generator.py \
  --use_bundled_pylibs -o gen generate -d ../source/ -I ../source/ \
  --bytecode_path gen/mojo/public/tools/bindings \
  --filelist=gen/services/network/public/mojom/url_loader_base__generator.rsp \
  -g c++ --typemap gen/services/network/public/mojom/url_loader_base__type_mappings
```

该 helper 只分派 `build/util/version.py` 与 `build/compute_build_timestamp.py`，会拒绝 mojom
生成器。本轮因此不改它、不伪造它：新入口在固定隔离进程内按同一 argv 直接调用生成器自己的
`main()`，helper 那一层被显式去掉并记录在 guard 的 `action.argv` 与 `entry` 里。

## 3. 必要输入：逐项标明层级，不把当前观测冒充历史资格

没有新产 parser/shared/type-mappings。下表比较的是 g-run-04 的 guard 所记录的 53 项暂存
摘要与历史回执；r2 的 provenance 函数并未逐项重读这 53 个暂存文件。不得把两份记录的对照
写成当前来源内容的完整复验（机器可读版本：`revision-r2/run-05/provenance-r2.json`）：

| 输入 | 数量 | 层级 | 依据 |
|---|---:|---|---|
| `-module` pickle | 50 | 历史回执 | 与 `native-content-interfaces-20261018-a/run-02/status.json`（`4d92a6f3…`，`stage=completed`、`producer_qualified=true`）的 `outputs` 同 logical 摘要逐项相同（审核时补核） |
| `url_loader_base__type_mappings` | 1 | 历史回执 | 同上，摘要 `206afdaa…` |
| `cpp_templates.zip` | 1 | 历史回执（input 侧） | 同一 U 回执的 `inputs` 以绝对路径记录该 zip，摘要 `4763b192…` 与当前字节相同 |
| `url_loader_base__generator.rsp` | 1 | `current_observation_only` | 该 RSP 由 Ninja 生成、不由生成器产出，历史回执里没有它的资格记录；本轮只记录当前字节 `d3adc4f6…` |

合计 52 项暂存记录可与历史回执对应，1 项 RSP 仅有 g-run-04 的当轮观测。r2 直接核对的是
记录中的摘要，不据此声称当前 53 项来源内容全部重读过；50 个 module 与 1 个 typemap 的
当前字节在前一轮审核时另有补核。此处也不表示 013 生产当时已完成该历史对照，不把保证范围
扩大到 U 回执以外的 runtime 源树：`source` 视图是整体复用、未逐文件重新做资格。

固定依赖：`/usr/bin/python3` `298a9e83…`；生成器入口
`mojo/public/tools/bindings/mojom_bindings_generator.py` `664123ac…`（19314 B）。

## 4. 完整输出与目标头

执行结果：**250/250 个声明产物全部产生**。缺失/额外两项由审核时对整棵 build 树的只读检查
确认（303 = 53 暂存 + 250 声明，missing=0、extra=0）；空文件与符号链接两项由本轮逐文件检查
确认（无空文件、无符号链接）。

| 项 | 值 |
|---|---|
| 声明输出 | 250 |
| 实际产生 | 250 |
| 目标头 | `gen/services/network/public/mojom/declarative_performance_observer.mojom-forward.h` |
| 目标头 bytes | **1010** |
| 目标头 SHA256 | `57c91e677e718be1c6353d20d4c0f6c51eddcfb496b5c11764d9dbac97faea8e` |

目标头内容为该 mojom 的真实前置声明（`DeclarativePerformanceObserverPolicy`、
`DeclarativePerformanceObserverPolicyDataView`、`PerformanceEntryType`、`StructPtr` 别名），
不是手写的空头，也没有为绕过 M 而删 IDL/import。

### 同一入口的字节级校验

在跑目标 owner 之前，先用同一入口跑一个**小例**：`mojom_connection_allowlist`（1 个
`.mojom`，5 个输出，5 个在已验收视图中已存在）。要求它把 5 个输出全部重产且**逐字节相同**：

| 输出 | 结果 |
|---|---|
| `connection_allowlist.mojom-forward.h` | 相同 |
| `connection_allowlist.mojom-import-headers.h` | 相同 |
| `connection_allowlist.mojom-test-utils.h` | 相同 |
| `connection_allowlist.mojom.cc` | 相同 |
| `connection_allowlist.mojom.h` | 相同 |

小例 5/5 逐字节相同，是该次小例的有效输出观测。g-run-03 的 launcher 在运行后被修改，
与该次 guard 摘要对应的原 launcher 字节已不可复现，因此不能把现存 v2 文件当作它的完整
源码身份凭证，也不能凭五个相同文件概括所有 owner 的生产者身份。g-run-04 的当前文件集合
由 §4.1 的只读补核另行确认；该集合门不能补回 g-run-03 已丢失的源码字节。

对照方向相反的一次比较：把新产生的 250 个文件名与已验收视图的 871 个文件按目录整体比较，
结果 `same=52 diff=0 only_new=250`。这里必须纠正一个容易误读的口径：**same 的 52 项不是
normal 生成输出**，它们是 50 个 `-module` + RSP + typemap 这些**暂存输入**（§3）。
把这 250 个声明成员与 same/only_new 求交，结果是 **`normal_same=0`、`normal_only_new=250`**，
即这 250 个 normal 输出此前一个都不存在、本轮全部为新增。因此这次比较只能说明「没有同名
normal 产物被改坏」，不能用来证明新旧 normal 产物等价。

### 4.1 完整集合门（013 r2 新增，非 g-run-04 当时已有）

g-run-04 的生产 worker 当时只遍历 `guard.declared_outputs` 并把这些键写进 status，判定函数
再比较同一批键，因此**声明之外的额外文件看不见**。r2 把这道门做成独立的生产函数
`check_actual_set()`：从 build 目录**独立枚举文件系统**，要求实际集合恰为「暂存输入 ∪ 声明
输出」，并对输出额外要求普通文件、非符号链接、非空。该函数已对真实 g-run-04 只读跑过一次：
`expected=303 actual=303 ok=True`（53 + 250，无 extra、无 missing）。

这一条是**本轮新增的验证**，g-run-04 当时并没有执行它；当时的 303 文件是审核时只读检查确认
的磁盘现状，不是当时代码已经实现了集合门。

## 5. 接入：作为只读 gen 前缀，优先于原 gen

新产物目录被作为**只读**前缀接入，插在 `-I../source` 之前，因此也排在原 `-Igen` 之前：

- 保留 012 的地址 SDK 与 storage overlay 两个前缀及其顺序；
- 原 `source`/`gen` 不写入，不重建任何历史视图；
- 前缀成员集合与摘要写在 guard 里（250 个成员逐个记录），不是「把任意未审目录提前」。

## 6. M 前沿：一次真实 Content `-M`

M 基线直接取 `native-address-sdk-20261019-a/m-run-03/status.json` 的 **319 项 argv**，加本轮
一个新 gen 前缀、更换 MF/crash/TMP，其余逐项顺序保持。逐元素核对结果：把新前缀从本轮 argv
中滤掉后，与 012 的 319 项基线相比**只有 2 项不同**，正是被更换的两个输出路径：

| 索引 | m-run-03（319 项） | 本轮（320 项） |
|---:|---|---|
| 201 | `-fcrash-diagnostics-dir=…/m-run-03/crash` | `-fcrash-diagnostics-dir=…/m-run-02/crash` |
| 317 | `…/m-run-03/delegate.pre.d` | `…/m-run-02/delegate.pre.d` |

即 argv = 319 + 1，没有回退到 317 项，没有丢地址层，没有新增 Linux/POSIX/Ozone 宏，没有
`-MG`，没有开 Content CXX 或链接。

真实结果：clang rc1，**clang 自身 143.199 秒**，launcher 记 147.286 秒（含准备与回收），
1 条 error，首错从 012 的 `navigation_handle.h:38` **推进到同文件 :41**：

```
../source/content/public/browser/navigation_handle.h:41:10: fatal error:
'third_party/blink/public/common/runtime_feature_state/runtime_feature_state_context.h' file not found
```

这条事实同时证明了三件事，因为它是一个 include 块内的顺序包含：

- `:33 net/base/ip_endpoint.h` 已解析 —— 012 的地址层仍从原 prefix 生效；
- `:38 declarative_performance_observer.mojom-forward.h` 已解析 —— 本轮前沿已闭合；
- `:39 device_bound_sessions`、`:40 web_sandbox_flags` 两个同批 forward 头也已解析。

本轮**共两次真实 clang**：第一次是 013 主执行（m-run-01，记录自相矛盾，见报告），第二次是
修正回执后的同前沿确认（m-run-02）。两次 clang 日志字节完全相同（807 B /
`ab0ead8db2f3aab865bcaa0013ed45728bb6525c0b0d4a24737fd3c0f433dbcb`），这是**重复观测一致**，
说明首错位置稳定；它不足以排除所有竞态，不作为「不存在抖动」的证明。

新首错 `third_party/blink/public/common/runtime_feature_state/runtime_feature_state_context.h`
是一个**新的缺失头**。它的归属（源码树自有头，还是某个生成动作的输出）**尚未定位**：包含路径
本身不区分源码与生成头，本轮没有为判定它而运行或生成任何东西。因此本轮不扩展 owner、
不继续生产下一个目标。

## 7. 入口与外部依赖（不声称仅靠仓库可重现）

本轮新增的执行入口不在仓库内，全部位于证据根
`F:/GTOS-Chromium/artifacts/native-network-mojom-forward-20261019-a`，逐字节摘要见
`manifest-013.json`：

| 文件 | 用途 |
|---|---|
| `launch-network-mojom-forward-v2.py` | 生成入口：取真实 rule argv、装配前缀、Landlock、超时、回收 |
| `mojom-forward-worker-v2.py` | 在隔离进程内调用生成器 `main()`，逐项校验输入与输出 |
| `decision-core.py` | 013 当时的判定函数（已被 r2 的 `gates-r2.py` 取代，字节冻结保留） |
| `decision-cases.py` | 该函数的纯数据案例（20 例，不启动 clang） |
| `launch-network-mojom-forward-m-v2.py` / `mojom-forward-m-worker-v2.py` | Content `-M` 入口 |
| `run-network-mojom-v2.sh` / `run-network-mojom-m-v2.sh` | 唯一启动方式（`env -i` + `unshare`） |
| `revision-r2/gates-r2.py` | r2 的共用判定函数：集合门、回执绑定门、deadline 预算判定、后代回收；函数小例通过，v3 完整入口未验收 |
| `revision-r2/entry-v3/launch-network-mojom-forward-v3.py`、`…-m-v3.py` | 调用上述门的下一版入口（本轮未运行） |
| `revision-r2/run-r2-gates.py`、`budget-r2.py` | 唯一测试入口与启动前 claim 的账本 |

r2 小例调用与 v3 接线共用的函数，但**完整 v3 入口未获执行验收**。新 runner 的真实数据
核对结果目前也未全部进入退出码判定，须在后续使用前修正。下列接线状态不代表 g-run-04 /
m-run-02 已执行这些新门，也不代表 v3 可直接运行：

1. **集合门**：生成入口在子进程退出后对 `OUT/build` 独立枚举，要求恰为暂存 ∪ 声明。
2. **回执绑定门**：M 入口的 250 个前缀成员，期望摘要取自 `g-run-04/status.json`
   （SHA `d2127d5b…`）里生成时的记录，当前字节逐个比它；`compare-url_loader_base.json`
   只提供成员名，不再用「现取 SHA 再当 accepted」。
3. **deadline 预算门**：函数小例验证准备耗尽时拒绝 spawn、等待额度扣除回收预留。v3 尚未
   运行，其 STARTED 在 gates 导入后，末尾校验又晚于 elapsed_total 采样；生成入口还存在
   阻塞式回收等待。因此不能宣称整个入口已受 600 秒硬截止约束，下一次执行前须补齐。
4. **后代回收**：判定函数硬性要求实测 `descendants_remaining == 0`，缺字段或非零即非零退出。

固定外部依赖：`/usr/bin/python3` `298a9e83…`；clang
`toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang` `656e4031…`；生成器入口
`664123ac…`；模板 `cpp_templates.zip` `4763b192…`。上述任一摘要变化，入口拒绝运行。

## 8. 未运行项

以下全部 **NOT_RUN**，不得从本轮结果推断：

- Content CXX 编译、链接、`content_shell` 可执行文件、原生浏览器启动；
- socket、传输、DNS、接口名服务、内核网络线格式、地址转换函数行为；
- GN/Ninja 再生成、旧 mojom 全链重跑、整个 `services/network` 构建；
- 内核/ISO/guest 重建、显示 VM 变更、新平台 ABI、新目标模式；
- 32×32 像素与 4096 字节上限未放宽。
