# mojom_core 之后的平台合同：现状盘点与提案

本文件记录一次**只读**的原生 ABI 盘点结果，以及据此提出的平台合同提案。

**结论**：本轮盘点把三类能力分开看——像素表面 ABI **已经存在且已验收**，但它没有出现在
本次 M 视图的 SDK 搜索根下；进程级窗口对象与输入事件**在本轮审查范围内未找到**；用户态
socket ABI **在本轮审查的公开 ABI、native 分发入口与 SDK 中未发现**。

**与上一轮 M 的关系**：真实 M 报的是两条 `native_ui_types` 平台错误加 `sys/socket.h`
缺失，**没有报 `surface_abi.h` 缺失**。表面 SDK 导出问题是本轮盘点发现，不是 M 首错的
直接原因；把表面头补进 SDK 也不会消除 `:189`/`:239` 或 `sys/socket.h` 那三条错误。

**范围**：本轮没有修改 Chromium、SDK、GN 或任何宏，没有编译、没有运行 M/CXX/浏览器。

## 已确认存在的原生能力

GTOS 原生 ABI 1 用 `int 0x80`，EAX 为调用/结果，EBX/ECX 为参数，`include/process/`
下共 50 个调用号。

### 像素表面（真实、已验收、进程可用）

`include/process/surface_abi.h` 定义四个调用：`0x4707` BEGIN、`0x4708` WRITE、
`0x4709` PRESENT、`0x470a` ABORT。

| 项 | 实际合同 |
|---|---|
| 句柄 | BEGIN 返回**正** signed-i386 句柄，标识一次**有主的提交事务**；到 `INT_MAX` 停止，不回绕，不再对新 owner 有效 |
| 格式 | RGBA8 premultiplied |
| 尺寸 | 32×32 上限，单帧 4096 字节，stride 必须精确 `width*4` |
| 提交 | 内核把完整用户块拷入 bounce buffer，校验每个 R/G/B ≤ A；PRESENT 拷入桌面快照后擦除草稿 |
| 事务退役 | PRESENT 成功后 `ScrubDraft()` 把 `drafting` 置假并清空 owner/handle，**原句柄不能继续 WRITE/PRESENT/ABORT**；桌面快照保留该值作为已发布结果的 `generation` 标识 |
| 失败 | 不改变草稿、句柄序列或已发布帧；已发布像素在生产者退出后仍存活 |

实际调用链已验证：`apps/png_surface_probe/native_main.cc:121-125` → `int $0x80` →
`src/process/native_runtime.cpp:787/798/819/820` → `src/process/native_surface.cpp`
（`Present` 在 :103-104 设 `front.generation = handle` 后立即 `ScrubDraft`）。

**两点必须区分**：已发布像素在生产者退出后仍存活，**不等于**存在一个进程可持续持有的
呈现对象；事务号退役后只是快照的 generation 标识，**不能直接当作 Chromium 的持久
`AcceleratedWidget`**。

**限制**：32×32 / 4096 字节的既有验收明确不覆盖浏览器尺寸表面。

### 事件（同步原语）

`include/process/event_abi.h` 定义 `0x4741..0x4746`（CREATE/DUP/CLOSE/SET/RESET/WAIT）。
WAIT 快照 1..16 个句柄，全部校验后才消费信号；超时用 PIT tick 而非毫秒。

这是**可等待事件**，不是输入事件队列。调用链：
`apps/native_events/probe/user.cpp:18,21` → `src/process/native_runtime.cpp:563-571` →
`src/process/native_events.cpp`。

## 未找到的能力

| 类别 | 判定 | 依据 |
|---|---|---|
| 进程级窗口对象 | 本轮审查范围内未找到 | 50 个调用号里没有窗口类；`include/gui/window.h` 是内核内部 C++ 类，不是 ABI |
| 进程级输入事件 | 本轮审查范围内未找到 | 键盘/鼠标由驱动直接投递给内核 GUI |
| 用户态 socket ABI | 本轮审查范围内未发现 | 公开 ABI 无 socket 调用号；`include/net/*.h` 是内核协议头（`uint32_t srcIP/dstIP`），不是 BSD `sockaddr` |

"未找到"是**有范围的结论**：检索范围是本轮审查的 `include/`、`src/`、`apps/`、`tests/`
与已解析的 SDK 真实路径，不声称整个工作区任何位置都没有。

关于网络要分三层说清，不能合并：

| 层级 | 事实 |
|---|---|
| 内核协议实现 | `src/net/{arp,etherframe,icmp,ipv4,tcp,udp}.cpp` 存在，Makefile 会编译 |
| 实例化点 | 本轮未找到；`src/kernel.cpp` 也没有引用它们 |
| 用户态 socket 服务 | 本轮审查范围内未发现 |

**未找到实例化点不等于网络栈一定没有在工作**，只是本轮没有找到证据。

## SDK 的 process 目录：两个位置不能混淆

`gtos_native_sdk` 下有**两个** `process` 目录，内容完全不同：

| 逻辑位置 | 实际文件 |
|---|---|
| `tools/gtos_native_sdk/process/` | `clock_abi.h`、`event_abi.h` |
| `tools/gtos_native_sdk/sdk/process/` | **`abi.h`、`environment_abi.h`、`file_abi.h`、`info_abi.h`、`tls.h`、`vm_abi.h`** |

本次 M 的搜索根是 `-isystem ../source/tools/gtos_native_sdk/sdk`，所以
`#include <process/X>` 解析到的是**第二个**目录。

消费者的实际 include 拼写是 `<process/abi.h>`、`<process/resource_abi.h>`、
`<process/surface_abi.h>`（`apps/png_surface_probe/native_main.cc:1-3`）。

按这个位置判断：

| 头 | `sdk/process/` 下 |
|---|---|
| `abi.h`、`environment_abi.h`、`file_abi.h`、`info_abi.h`、`tls.h`、`vm_abi.h` | **已导出** |
| `surface_abi.h`、`resource_abi.h`、`thread_abi.h`、`realtime_abi.h`、`readonly_abi.h` | 未出现 |

所以"SDK 顶层没有 `file_abi.h`"**不等于** `<process/file_abi.h>` 无法解析——它本来就在
`sdk/process/` 下。判定的四项必须是：logical 路径、真实路径、实际 include 拼写、搜索根。

## 两处 `#error` 分属两条链

`ui/gfx/native_ui_types.h` 有两条互相独立的条件链：

| 链 | 起始 | 分支 | 落点 |
|---|---|---|---|
| NativeView/NativeWindow/NativeEvent | `#if defined(USE_AURA)` | Aura → iOS → Mac → Android → else | :189 |
| AcceleratedWidget | `#if BUILDFLAG(IS_WIN)` | Win → iOS → Mac → Android → **Ozone** → else | :239 |

第一条链**没有** Ozone 分支。所以只定义 `USE_OZONE` 只能消除 :239，不能消除 :189。

## 三个维度不是同层三选一

| 维度 | 回答的问题 |
|---|---|
| 目标身份（如新增 `IS_GTOS`） | 这个目标是 GTOS 吗 |
| `USE_AURA` | 是否用 Aura 的窗口/事件对象框架 |
| `USE_OZONE` | 是否用 Ozone 的显示/输入平台抽象 |

三者可以同时成立，也可以只成立一部分。把它们当作互斥三选一是错的。

## 推荐的最小下一步

**只补 `surface_abi.h` 及其真实必要依赖到 `sdk/process/`，并按真实消费者需求评估。**

理由：`sdk/process/` 已经有 6 个 ABI 头。`apps/png_surface_probe` 同时直接包含
`process/resource_abi.h` 和 `process/surface_abi.h`，两者均未出现在该目录中。
`surface_abi.h` 自身没有 include 依赖；是否一并导出 `resource_abi.h`，
应由要验证的真实消费者决定，不批量增加其他头。

**这一步不推进 M 首错**：补 surface 头不会消除 `native_ui_types.h:189`/`:239`，也不会
消除 `sys/socket.h`。它的作用是为后续原生桥接准备接口，不是让当前 M 通过。

窗口与输入事件需要新的进程级 ABI 设计（当前没有窗口对象、没有输入事件投递）；
socket 需要先定用户态 ABI，因为 `net/base/ip_endpoint.cc` 用到真实的
`sockaddr_in`/`sockaddr_in6` 布局与 `sizeof`，不能靠复制宿主头或空 typedef 消除。

若将来把 surface 事务号当作整数 token 使用，必须另外定义稳定的呈现对象及其生命周期合同；
本轮不设计也不实现它。

本文件只记录提案状态，**不表示平台适配已经完成**，也不表示 M 已通过或浏览器可运行。
