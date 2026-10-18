# 原生图像 ABI 的最小 SDK 导出

本文件记录把 GTOS 已有的两个原生图像 ABI 头导出成一个**窄叠加目录**的工具与验证结果。

**结论**：`process/resource_abi.h` 与 `process/surface_abi.h` 可以按原字节导出到一个新的
叠加目录，放在原 SDK 搜索根之前即可让真实消费者的 `<process/...>` 包含解析成功；本次没有写原 SDK，
仓库两个源头摘要前后相同。

**这不是平台集成完成**。SDK 叠加可用、Content M 仍有既知平台缺口、32×32 表面限制与事务
退役合同，是三件不同层级的事，本文件只覆盖第一件。

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

所以只导出这两个：`abi.h` 继续从原 SDK 解析，其余五个已存在头不重复导出，也不增加任何
无消费者的头。

## 工具

`tools/export-native-graphics-sdk.py`，固定两个成员，不接受任意来源或命令参数。

| 行为 | 合同 |
|---|---|
| 来源 | 只从工具自身位置定位仓库，只读 `include/process/resource_abi.h` 与 `include/process/surface_abi.h` |
| 输出 | 只接受一个**新目录**；目标为 `<output>/sdk/process/{resource_abi.h,surface_abi.h}` |
| 复制 | 按原字节复制，逐个回读目标摘要与来源比较 |
| 拒绝 | 输出目录已存在、目标已存在、来源不是普通文件或越出仓库，一律在写入前拒绝 |
| 不写 | 不触碰原 SDK、不触碰仓库头、不复制原 SDK 整树 |
| manifest | 记录 `checkout` 以及每个成员的 logical 名、target 路径、字节数、SHA256，并列出**故意不导出**的六个头及原因。manifest **没有独立的 source 路径字段**——源路径由工具的固定规则 `checkout/include/<logical>` 推导 |

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

## 未做

- 没有运行 Content M、完整 consumer、MG、CXX、链接、ELF32、浏览器或 VM；
- 没有运行 `png_surface_probe` 的完整 TU（其本地 include 需要已生成的 PNG/Wuffs 产物，
  本轮未提供，标记为 NOT_RUN）；
- 没有修改原 SDK、仓库头、ABI 号、布局或内核；
- 没有设计窗口、输入事件、socket ABI，也没有引入 `IS_GTOS`/`USE_AURA`/`USE_OZONE` 分支。

## 与其他层级的区别

| 层级 | 状态 |
|---|---|
| SDK 叠加导出这两个头 | **可用**（本轮验证） |
| Content M 的两条 `native_ui_types` 平台错误 | **仍存在**，与本次导出无关 |
| `sys/socket.h` 缺失 | **仍存在**，属原生 SDK 接口缺口 |
| 32×32 表面限制、事务退役合同 | **未变** |

补这两个头不会让当前 Content M 通过。它的作用是把已有接口纳入可用的 SDK 布局，为后续
原生桥接做准备。
