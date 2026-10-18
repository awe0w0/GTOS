# mojom_core 产物接入与首次真实 M 依赖诊断

本文件记录把上一轮 667 个 mojom_core 生成产物接入 Content delegate 最小诊断视图后，
**一次真实 `clang -M` 预处理依赖扫描**的结果。

**结论**：`frame.mojom-forward.h` 缺失已经不再出现——新产物确实被消费。新的首个真实
错误是 `ui/gfx/native_ui_types.h:189` 的平台分支缺口。

**范围**：只运行了依赖扫描（`-M`）。完整旧 consumer、MG、CXX、MD==M、ELF32、浏览器、
网页/视频/HTML5、VM **均未运行**。诊断成功不等于 CXX 或浏览器可用。

## 接入方式

不把整个生成树塞进视图，只按清单叠加本次产物：

| 项 | 值 |
|---|---|
| 新产物清单 | `run-02/status.json` 的 667 条 `outputs` |
| 历史基础视图 | 既有 delegate 运行的 gen 树（只读引用，6507 文件 / 367 目录） |
| 冲突表 | 逐条查同名文件：**667 条全部为新增**，同字节 0，不同字节 0 |
| 当前字节绑定 | 667 条源路径/期望摘要/实际字节逐条比较：expected 667、attempted 667、matched 667 |

历史基础视图只作 `historical_view_reference` 复用，本次没有重新做六包资格，也没有
重跑旧 consumer。667 条新产物单独记为 `current_byte_bound`。

### 视图构建的一个真实缺陷

历史 gen 树里的 `ui/color` 是一个**指向目录的符号链接**（解析到旧的 graph-30 生成树）。
早期实现用"是否目录（不跟随链接）"判断节点类型，目录链接因此落进文件别名分支，overlay
复制时穿过这条链接，试图写入只读来源，被沙箱正确拒绝。

修法是调整判断顺序——**先判断该路径是否为必须展开的祖先目录，再判断节点类型**。必须
展开的目录链接会被解析、验证仍在固定工作区内且确为目录、记录链接原文与真实目标，然后
在目标端建成**真实目录**。沙箱权限没有放宽；最终修复没有更换叠加策略，只是让祖先链接
不再存在，从而不需要穿过它。

修复用同一沙箱内的五个小夹具先验证，再跑正式视图。夹具只覆盖**目录形状与分支选择**，
其"来源"目录建在本次可写输出内，不是对历史只读区的拒写实测：

| 夹具 | 观测结果 |
|---|---|
| 目录链接必须展开 | 基树 `color` 确为目录链接；新目标两级均为真实目录；复制与摘要回读成功；探针未落进夹具的来源目录 |
| 祖先在基础树中不存在 | 真实目录被预建，复制成功 |
| 展开祖先为普通文件 | 命中"祖先不是目录"门 |
| 展开祖先链接越界 | 命中工作区边界门 |
| 非展开的目录链接 | 保留只读别名，不递归复制 |

## 真实诊断结果

| 项 | 值 |
|---|---|
| 入口 | 固定 launcher（`M` 模式） |
| 入口返回码 | **0**（`launcher-result.returncode`），`status_pass` 为真，`failure` 为空 |
| 视图 | 真实目录 10、别名 979、解析链接 1、文件 811、复制 667、去重 0 |
| clang 启动 | **1 次**，pid 730，未超时，日志未超限 |
| clang 返回码 | **1**（`status.M.returncode` 与 `launcher-result.clang_returncode`） |
| 错误数 | 3 |
| 依赖文件 | 未产生（编译未通过） |

入口返回 0 表示诊断流程走完，clang 返回 1 表示预处理未通过，两者是不同的字段。
三个耗时采样点不强行相等：driver 自记 `status.seconds` 为 238.182 秒，launcher 自记
`launcher-result.seconds` 为 238.695 秒，宿主收到 launcher 打印的 `seconds` 为 238.701 秒；
其中 clang 本身 104.669 秒。

### 缺失头已解决

历史观察是 `content/public/browser/render_frame_host.h:45` 找不到
`third_party/blink/public/mojom/frame/frame.mojom-forward.h`。

本次 include 链已经越过该行、继续到 `:52`：

```
render_frame_host.h:45  #include "third_party/blink/public/mojom/frame/frame.mojom-forward.h"
render_frame_host.h:52  #include "ui/gfx/native_ui_types.h"
```

第 45 行的 include 已解析成功，诊断视图内该头确实存在，2430 字节，
SHA256 `1ec9ffcb6094438482502457db0ae809b6fd94ff9294b46e75358b72f7478cd3`。

### 新的首个错误

```
../source/ui/gfx/native_ui_types.h:189:2: error: Unknown build environment.
../source/ui/gfx/native_ui_types.h:239:2: error: unknown platform
../source/net/base/ip_endpoint.h:32:10: fatal error: 'sys/socket.h' file not found
```

三条错误的性质（`ui/gfx/native_ui_types.h` 实际有**两条互相独立**的分支链）：

| 错误 | 所属链 | 原因 |
|---|---|---|
| `native_ui_types.h:189` | `NativeView`/`NativeWindow`/`NativeEvent` 链，约 :122 起 | 首项是 `#if defined(USE_AURA)`，随后 `IS_IOS`、`IS_MAC`、`IS_ANDROID`，最后 :188 `#else` 落到 :189。**这一链没有 `IS_OZONE` 分支**，所以只加 `USE_OZONE` 不会改变它 |
| `native_ui_types.h:239` | `AcceleratedWidget` 链，约 :218 起 | 分支为 `IS_WIN`/`IS_IOS`/`IS_MAC`/`IS_ANDROID`/**`IS_OZONE`**，最后 :238 `#else` 落到 :239。这一链才认 `IS_OZONE`，而 `IS_OZONE` 由 `build/build_config.h` 的 `defined(USE_OZONE)` 决定，本次编译参数未定义它 |
| `ip_endpoint.h:32` | `net/base/ip_endpoint.h` | `IS_WIN` 为 0 走 POSIX 分支 `#include <sys/socket.h>`；该头在**本次固定的原生 include 搜索路径下无法解析**，属于 SDK 接口缺口 |

因此两处 `#error` **不能合并成一个原因**：:189 需要的是 `USE_AURA` 或一条新的 GTOS 分支，
:239 需要的是 `IS_OZONE`/`USE_OZONE` 或同样的 GTOS 分支。只补 `USE_OZONE` 只能消除 :239，
不能消除 :189。本文不把这两种做法列为可互换的修法。

`sys/socket.h` 这一条只说明本次固定 include 路径下解析不到它，不证明整个工作区任何位置
都没有该文件，也不意味着复制一份 Linux 头就能获得 GTOS 的 socket ABI。

这三条都不是 mojom 生成产物能解决的，也不是本次诊断的接线失误——接线本身已经正确
（第 45 行已解析即为证据）。

## 后验与隔离

| 项 | 值 |
|---|---|
| 沙箱 | Landlock ABI 7，独立 user/net 命名空间，`no_new_privs` |
| 工作区 | 只读；仅本次输出目录可写 |
| 来源写权限 | 由 Landlock 禁止；`guard.source_write_denied` 为真（这是对一个固定工作区只读路径单独试写的实测结果） |
| 夹具 | 只检查目录集合与"探针未落进夹具来源目录"，**不构成**对历史只读区的拒写实测 |
| 667 条新产物 | 写入前先验摘要（expected 667 / attempted 667 / matched 667），复制后逐个回读摘要一致 |
| 入口末绑定项 | `final_bound_hashes` 实际为 **4 项**：driver、launcher、contract、cc；入口结束时这 4 项均未变 |
| 子进程回收 | `all_own_descendants_reaped` 为真，收养 0 |
| 心跳 | 60 秒定时线程 |

后验的边界要说清：本轮**没有**对整棵历史来源做内容复读，也**没有**为全部暂存或输出节点
建立并复核节点身份基线。上面 4 项是入口末的绑定项摘要，不是"所有来源内容与节点全程未变"。

### 本次证据的绝对路径与摘要

| 文件 | SHA256 |
|---|---|
| `F:/GTOS-Chromium/artifacts/native-content-mojom-core-consumer-20261019-a/mojom-core-consumer-v7.py` | `75f8e74d4f3c00a12de764dbe386fe088a6613204c1ba228a4f71659b117b66a` |
| `F:/GTOS-Chromium/artifacts/native-content-mojom-core-consumer-20261019-a/launch-core-consumer-v7.py` | `d931e90e2cdbb9e06c3088f4001f0d79c5aa9eaf7efb1cb8c3bc9829ac092344` |
| `F:/GTOS-Chromium/artifacts/native-content-mojom-core-consumer-20261019-a/run-07/status.json` | `24fddf7c3e52a3844383dc3d71138730748251b5926f6aa98361be5ce74fefd8` |
| `F:/GTOS-Chromium/artifacts/native-content-mojom-core-consumer-20261019-a/run-07/launcher-result.json` | `d65d9ec5d3a90e6957bf82f2af6067e7521d4c7c85470c1b8d6696638dff520d` |
| `F:/GTOS-Chromium/artifacts/native-content-mojom-core-consumer-20261019-a/run-07/delegate-M.log` | `353aeb494d62553c729b3e1e8ea38cd640ab06a53a170f711fcc2ac6f21012e4` |

源码与产物均在仓库之外。本仓库的内容本身不能独立复现这次外部运行——上面这些摘要只
说明当时那一次外部运行的字节，不提供重放环境。

## 下一步

需要先评估 GTOS 现有的原生窗口/事件/表面接口与 socket 接口，再决定走 Aura/Ozone 集成
还是新增 GTOS 分支，并把选择写成具体合同。两处 `#error` 落在不同链上，修法不能混用：

- :189 所在链只认 `USE_AURA` 或 Apple/Android 平台，`USE_OZONE` 对它无效；
- :239 所在链认 `IS_OZONE`（即 `USE_OZONE`）或 Apple/Android/Windows。

本次没有修改 Chromium、SDK、GN 或任何宏，没有追加新的生成 owner，也没有新定义宏去假装
已有窗口、事件或表面后端。在平台语义确定之前，继续做 CXX 编译不会得到有意义的结论。
