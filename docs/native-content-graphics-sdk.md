# 原生图像 ABI 的最小 SDK 导出

本文件记录把 GTOS 已有的原生图像 ABI 头导出成一个**窄叠加目录**的工具与验证结果，
以及随后加上的窗口 opt-in 模式与用户态六调用包装。

**结论**：`process/resource_abi.h` 与 `process/surface_abi.h` 可以按原字节导出到一个新的
叠加目录，放在原 SDK 搜索根之前即可让真实消费者的 `<process/...>` 包含解析成功；本次没有写原 SDK，
仓库两个源头摘要前后相同。

窗口链另有一个**显式可选**模式 `--with-window`，在上述两头之外再导出
`process/window_abi.h` 与 `process/window_client.h`；**默认调用仍恰好导出原两头**。

**这不是平台集成完成**。SDK 叠加可用、Content M 仍有既知平台缺口、32×32 表面限制与事务
退役合同，是三件不同层级的事，本文件只覆盖第一件。窗口部分同理：可用的用户态接口不等于
浏览器平台已接入。

## 为什么是这两个头

`apps/png_surface_probe/native_main.cc` 的前三条 include 是：

```
#include <process/abi.h>
#include <process/resource_abi.h>
#include <process/surface_abi.h>
```

本次 M 视图的搜索根是 `-isystem .../tools/gtos_native_sdk/sdk`，而该 SDK 的 `sdk/process/`
下已经有 `abi.h`、`environment_abi.h`、`file_abi.h`、`info_abi.h`、`tls.h`、`vm_abi.h`
六个头。缺的只有 `resource_abi.h` 与 `surface_abi.h`。

所以默认只导出这两个：`abi.h` 继续从原 SDK 解析，其余五个已存在头不重复导出，也不增加任何
无消费者的头。窗口链的两个头属于同一套缺口，但因为只有窗口消费者需要，所以放在可选模式里，
不让所有既有调用方被动多出两个文件。

## 工具

`tools/export-native-graphics-sdk.py`，成员集合固定，不接受任意来源或命令参数。

| 行为 | 合同 |
|---|---|
| 来源 | 只从工具自身位置定位仓库，只读 `include/process/` 下固定成员 |
| 默认成员 | 恰好 `resource_abi.h`、`surface_abi.h` 两个 |
| `--with-window` | 上述两个 **加** `window_abi.h`、`window_client.h`，共四个 |
| 输出 | 只接受一个**新目录**；目标为 `<output>/sdk/process/<成员名>` |
| 复制 | 按原字节复制，逐个回读目标摘要与来源比较 |
| 拒绝 | 输出目录已存在、目标已存在、来源不是普通文件或越出仓库，一律在写入前拒绝 |
| 不写 | 不触碰原 SDK、不触碰仓库头、不复制原 SDK 整树 |
| manifest | 记录 `checkout`、`mode`（`graphics-only` / `with-window`）以及每个成员的 logical 名、target 路径、字节数、SHA256，并列出**故意不导出**的六个头及原因。manifest **没有独立的 source 路径字段**——源路径由工具的固定规则 `checkout/include/<logical>` 推导 |

不导出给应用的东西是明确的：内核侧 `native_windows.h`、GUI provider 与 C++ 内核类都不是
应用接口，任何一个模式都不会复制它们。


## 验证结果

固定隔离入口（Landlock ABI 7、独立 user/net 命名空间、`no_new_privs`、真实 subreaper）
跑一次，四项检查全部通过：

| 检查 | 预期 | 实际 |
|---|---|---|
| 导出成功 | 两份成员字节/摘要与来源相同；manifest 逻辑名与真实目标一致 | **通过**。`resource_abi.h` 2673 字节 `2465560e…`、`surface_abi.h` 3636 字节 `44e3194b…` |
| 输出已存在拒绝 | 同一入口再次指向已有目标必须在写前拒绝，且原目标内容不变 | **通过**。rc=1，消息 `refused: the output already exists`；`existing_targets_unchanged: true` |
| 原 SDK 缺 surface 的对照 | 只含 `<process/surface_abi.h>` 的小 TU 在原 SDK 路径下失败 | **通过**。rc=1，日志点名缺失该头，0.219 秒 |
| 叠加后组合包含 | 真实消费者前三条 include，以叠加在前、原 SDK 在后做 `-fsyntax-only` | **通过**。rc=0，0.118 秒 |

### 包含来源（`-H` 实测）

| 头 | 实际来源 |
|---|---|
| `process/abi.h` | **原 SDK**（`.../gtos_native_sdk/sdk/process/abi.h`） |
| `process/resource_abi.h` | **新叠加**（`run-02/overlay/sdk/process/resource_abi.h`） |
| `process/surface_abi.h` | **新叠加**（`run-02/overlay/sdk/process/surface_abi.h`） |

即叠加目录确实只补了两个成员，`abi.h` 仍来自原 SDK，没有用 `-I` 指向仓库 `include/`
绕过导出路径。

### 原头的实际覆盖范围

上面这些是**仓库两个源头**的摘要前后不变，以及原 SDK 中这两个**原本就不存在**的成员
路径前后仍不存在（该部分记录为 `{}`）。

这**不是**对原 SDK 六个既有头或整棵 SDK 的内容后验：本轮没有对原 SDK 做全量摘要复核。
原 SDK 的写权限由沙箱禁止，但那只是权限约束，不是内容比对。

### 入口失败判定

导出入口的最终退出码与它写下的回执 `returncode` 来自**同一个判定结果**，该判定要求同时
满足：子进程 code 为 0、未超时、没有收养子进程、绑定未变、没有 failure、status 确实完成、
四项检查全部通过。任一条不满足即非零。

这套判定另有独立的小例验证（正常 0；分别注入子进程非零、超时、收养子进程、绑定变化、
failure、未完成、缺检查，全部非零），以及负例门（缺头诊断必须命中完整的
`fatal error: 'process/surface_abi.h' file not found`，拒绝消息必须命中已有输出拒绝，
timeout/124、`#error` 变体、traceback 等伪样本被拒）。这些是**新做的小例**，
不是 run-02 那次运行的产物。

### 实际入口与回执

| 项 | 路径 |
|---|---|
| 导出工具 | `tools/export-native-graphics-sdk.py` |
| 正常运行（四项检查） | `artifacts/native-content-graphics-sdk-20261019-a/run-02/` |
| 判定小例 | `artifacts/native-content-graphics-sdk-20261019-a/decide-01/` |


### 编译目标

固定 `--target=i686-unknown-none-elf`、`-ffreestanding`、`-nostdinc`/`-nostdinc++`、
`-D__GTOS__=1`、`-DGTOS_NATIVE_SDK=1`、`-fno-exceptions`/`-fno-rtti`，只用原 SDK、
`c-sdk` 与 clang 内建 include，不借用宿主 Linux 头。两次调用都是 `-fsyntax-only`，
不产生 `.o` 或可执行文件。

头里原有的静态布局断言（`sizeof` 与 ABI 常量的 `static_assert`）在编译时自然执行，
没有另写一套镜像断言。

## 窗口用户态包装

`include/process/window_client.h` 是 `window_abi.h` 的**用户态客户端**，不是 ABI 的一部分：
它不新增 opcode、字段、上限或策略，只把调用者的请求指针与精确字节数原样交给 `int $0x80`。

六个函数都在 `gtos::process` 内，返回类型一律是 `int`：

| 函数 | 对应调用 | 返回 |
|---|---|---|
| `WindowCreate` | `GTOS_SYS_WINDOW_CREATE` | 新句柄为**正数**；失败为内核负 errno |
| `WindowSubmit` | `GTOS_SYS_WINDOW_SUBMIT` | **实际接收的字节数**；失败为负 |
| `WindowPresent` | `GTOS_SYS_WINDOW_PRESENT` | 0 或负 errno，不转成 bool |
| `WindowClose` | `GTOS_SYS_WINDOW_CLOSE` | 0 或负 errno；第二次为 `BAD_HANDLE` |
| `WindowInfo` | `GTOS_SYS_WINDOW_INFO` | 0 或负 errno；结果写往 `result_va` |
| `WindowInputRead` | `GTOS_SYS_INPUT_READ` | **事件条数**（0 表示队列空且一个字节都没拷）；失败为负 |

明确的"不做"清单：

- **不预检**。字节数错、请求指针为 0、陈旧句柄都送到内核去拒；包装若在用户态先挡，
  内核负例就退化成用户态负例，证明力为零。
- **不改写结果**。正句柄仍是正句柄，错误仍是内核原值，SUBMIT 仍是长度而不是状态，
  INPUT_READ 仍是条数而不是字节；没有任何一处窄化成 8/16 位或 bool。
- **不持有所有权**。不隐式关闭句柄；应用 `CLOSE` 是销毁，与桌面 `RequestClose` 的幂等
  不是同一件事，两者不混用。
- **无 transport、无分配、无异常、无线程/等待框架**。
- **编译期拒绝非目标**。非 `__i386__`、或编译给宿主 OS（`__linux__`/`__unix__`/
  `__APPLE__`/`_WIN32`）一律 `#error`，避免在 Linux 下静默调用同号的宿主接口。

## 窗口链的验证结果

一批跑完，入口一次调用（`tests/native_window_sdk_test.sh`）：

| 检查 | 预期 | 实际 |
|---|---|---|
| 默认导出成员 | 恰好 `resource_abi.h`、`surface_abi.h` | **通过**。目录内仅这两个头加 manifest，`mode: graphics-only` |
| opt-in 成员 | 上述两个加 `window_abi.h`、`window_client.h` | **通过**。四个头加 manifest，`mode: with-window` |
| 已存在输出拒绝 | 写前拒绝且原目标字节不变 | **通过**。rc=1，消息点名已有输出；前后摘要逐行相同 |
| 成员字节 | 与仓库源逐字节相同 | **通过**。四个成员的 SHA256 与 `include/process/` 一致 |
| wire 头 C11 | `-nostdinc` 下单独编译通过，头内 `_Static_assert` 生效 | **通过**。不借用任何标准头，产出非空对象 |
| C++ 消费者 | 只用"叠加 + 原 SDK + c-sdk"，i686 freestanding | **通过**。`-H` 显示见下 |
| 真实调用点 | 六个 `int $0x80`，六个 opcode，ebx/ecx 传参 | **通过**。反汇编只取助记符列，得 6 个陷阱、`0x4750`–`0x4755`、精确字节数 |

### 包含来源（`-H` 实测）

| 头 | 实际来源 |
|---|---|
| `process/resource_abi.h` | **新叠加** |
| `process/window_abi.h` | **新叠加** |
| `process/window_client.h` | **新叠加** |
| `process/abi.h` | **原 SDK** |
| `process/vm_abi.h` | **原 SDK** |

编译命令行里没有任何指向仓库 `include/` 的 `-I`；脚本另外检查 `-H` 记录中不出现仓库
`include` 路径，出现即失败。所以"头来自 SDK"是被记录的，不是被假设的。

### 现有 probe 的接线

`tests/native_window_probe_build.sh` 现在先在本轮 OUT 内用 `--with-window` 生成叠加，再以
`-isystem <叠加>/sdk -isystem <原 SDK>/sdk -isystem <原 SDK>/c-sdk` 编译
`apps/native_window_probe/native_main.cc`，并把 `-H` 的来源清单留档、逐项断言：
两个窗口头必须来自叠加，`abi.h`/`vm_abi.h` 必须来自原 SDK，任何生产头来自仓库 `include`
即失败。原 PNG 已资格对象继续复用，复用绑定不变。

probe 内部的分工是刻意的：

- 正常窗口调用（含 `BAD_STATE`、`BAD_HANDLE`、`BAD_PIXELS`、`RANGE`、`LIMIT`、第二次
  `CLOSE` 这些**格式合法、由内核按策略拒绝**的负例）走 SDK 包装；
- 故意构造的**坏版本 / 坏请求大小 / 坏目标指针**走 `raw*` 帮助函数（裸 `int $0x80`），
  以免将来包装一旦长出预检就把内核负例拦在用户态；
- 另有 8 处请求指针直接为 0 的裸调用沿用原样，未改。

guest 日志因此同时报告两条路径的实际次数（见下），任一侧为 0 即判失败。

### 一次集中 guest 的实际结果

审计过的 `kernel-04` 内核 + 本轮的包装 probe + 已审四槽 ISO 构建器 + 已审主 runner，
一次运行通过 21/21 项检查，阶段计数 `INPUT_COPY_DONE 20`、`RESET_COPY_DONE 11`、
`CLOSE_COPY_DONE 8`，精确像素与 `48/8/40` 完整目标核对仍在，退出 0。日志新增一行：

```
GTOS NATIVE WINDOW SDK CALLS V1 287 27
```

即 287 次正常调用经 SDK 包装、27 次非法请求走裸路径；27 与 probe 源码里的 `raw*`
调用点数相同，`tests/native_window_sdk_guest_check.py` 会独立核对这一点。


## 未做

- 除上述集中测试 guest 外，没有运行 Content M、完整 consumer、MG、CXX 或浏览器；未新启动展示 VM；
- 没有运行 `png_surface_probe` 的完整 TU（其本地 include 需要已生成的 PNG/Wuffs 产物，
  本轮未提供，标记为 NOT_RUN）；
- 未修改原 SDK、既有 wire 头、ABI 号、布局、权限或内核；新增用户态客户端见上文；
- 没有设计 socket ABI，也没有引入 `IS_GTOS`/`USE_AURA`/`USE_OZONE` 分支；
- 没有把窗口接口接到 Chromium：原生 Chromium 从未运行，浏览器尺寸表面、NativeView
  与平台宏仍是后续工作，32×32/4096 不构成浏览器尺寸资格。

## 与其他层级的区别

| 层级 | 状态 |
|---|---|
| SDK 叠加导出这两个头 | **可用**（本轮验证） |
| 窗口两头 + 六调用包装 | **可用**（本轮验证，opt-in） |
| Content M 的两条 `native_ui_types` 平台错误 | **仍存在**，与本次导出无关 |
| `sys/socket.h` 缺失 | **仍存在**，属原生 SDK 接口缺口 |
| 32×32 表面限制、事务退役合同 | **未变** |

补这两个头不会让当前 Content M 通过。它的作用是把已有接口纳入可用的 SDK 布局，为后续
原生桥接做准备。
