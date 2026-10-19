# 持久呈现对象与输入队列的最小 ABI 合同

本文件是下一轮内核/用户 ABI 实现的**权威规则表**。状态表、调用序列、wire 与测试预期
全部由 §1–§4 的规则推导，不另行定义。

**状态**：拟议，未实现。本轮未修改内核、系统调用分发、生产 ABI 头、Chromium、SDK 或 GN。
本文件 §1–§6 仍是规则来源；§7.8 只记录 r6 已实际执行的分支，与规则冲突时以 §1–§6 为准。

## §0 与已有合同的关系

| 已有事实 | 处置 |
|---|---|
| 旧 surface 事务（`0x4707..0x470a`），PRESENT 后退役 | **保留不改** |
| 旧快照 `generation` 是已发布结果标识 | **保留**，不改名成窗口句柄 |
| 旧像素在生产者退出后仍存活 | **保留**，新对象销毁不改这条 |
| 旧 `imageProvider` / `ModernImage` / 关闭路径 | **保留**，新对象加法接入 |
| 同步 event（`0x4741..0x4746`） | 与输入队列是不同对象 |
| 32×32 / 4096 字节上限 | **不放宽**，仍不满足浏览器尺寸 |

---

## §1 帧状态、对象身份与关闭语义

### 1.1 只有两种状态维度

| 维度 | 取值 | 说明 |
|---|---|---|
| **生命周期** | `live` / `closing` / `dead` | 唯一决定"能不能操作" |
| **帧数据** | `progress`、`total`、`frame_generation` | 附属数据，不决定能否操作 |

`EMPTY` / `DRAFTING` / `PUBLISHED` **只作为 `WINDOW_INFO` 的派生显示值**，不能替代操作条件：

| 派生显示 | 条件 |
|---|---|
| `DRAFTING` | `progress > 0`（**包含 `progress == total`**） |
| `EMPTY` | `progress == 0` 且 `frame_generation == 0` |
| `PUBLISHED` | `progress == 0` 且 `frame_generation > 0` |

**完整草稿在发布前仍显示 `DRAFTING`**，不是 `PUBLISHED`。

### 1.2 PRESENT 的唯一成功前提

句柄/owner 有效 **且** `lifecycle == live` **且** `progress == total` **且**
`frame_generation` 未耗尽。

成功：替换 front → `frame_generation++` → `progress = 0` → 清 draft。
**再次 PRESENT 必然 `BAD_STATE`**（因为 `progress` 已归零，不再等于 `total`）。

### 1.3 半帧重启：第一版直接拒绝非连续 offset

**选择：`SUBMIT` 要求 `offset == progress` 且 `length` 使 `progress + length <= total`。**
非连续 offset 一律 `BAD_STATE`。

理由：要支持"半帧后从 0 重来"就必须保证"坏重启不清旧 draft"的原子性——先完成请求快照、
范围检查、用户数据复制、全块像素校验，再一次提交重置与写入。第一版不值得引入这个
两阶段提交。**重启草稿的唯一方式是销毁并重建对象。**

这样避免了 r2 留下的"先清 draft 再失败"风险，也不需要额外的原子性机制。

### 1.4 对象身份是句柄，不是帧 generation

| 项 | 规则 |
|---|---|
| 对象身份 | **本次 `CREATE` 返回的正句柄**，不回绕复用 |
| 句柄耗尽 | 独立错误 `HANDLE_EXHAUSTED`，对象无法再创建 |
| 帧 generation 耗尽 | 独立错误 `OVERFLOW`，该对象无法再 PRESENT，但句柄仍有效 |
| `frame_generation` 用途 | **仅**判断帧是否更新，**不参与** focus/capture/路由身份 |
| 槽重用 | 换新句柄；**旧句柄永远不能路由到新对象** |

不发明额外的"对象 generation"。路由与校验只认句柄。

### 1.5 关闭语义：请求与销毁是两件事

| 操作 | 语义 | 重复调用 |
|---|---|---|
| `Desktop::RequestClose(handle)` | `live → closing`，至多产生**一次** CLOSE 事件 | **幂等**：再次请求不再排 CLOSE，即使原 CLOSE 已被读出 |
| 应用 `WINDOW_CLOSE` | **销毁**：`live` 或 `closing` 时成功一次，**立即撤销句柄** | 之后同句柄 `BAD_HANDLE`。**不是**幂等请求 |
| owner 退出 / fault / reap | 同样**销毁** | — |

**不得在销毁或清队列之后向已销毁对象投递 CLOSE。** 销毁后句柄立即无效。

### 1.6 closing 的可见与可操作范围

| 项 | closing 时 |
|---|---|
| 最后帧可见 | **保留**（窗口仍显示） |
| 普通输入 | **拒收** |
| `SUBMIT` / `PRESENT` | `BAD_STATE` |
| `WINDOW_INFO` | 允许 |
| `INPUT_READ` | 允许，**只能读到那一条 CLOSE** |
| 资源释放时机 | 应用 `CLOSE` 或 owner 回收才释放 |

**没有自动超时**——不引入新的睡眠调度系统。

---

## §2 INPUT_READ：一次完整的生产事务

### 2.1 已有生产依据（已只读核对）

`src/process/native_events.cpp:96-108` 的端点模式：函数入口一个
`InterruptGuard guard;`（`:97`），随后 `CopyFromUser` 快照请求（`:101`）、校验
（`:102-103`）、`CopyToUser`（`:107`），全程不解除保护。`:106` 的注释明确
"IF=0 on BSP: publication cannot fail after this fully checked copy"。

`InterruptGuard`（`include/memory/criticalsection.h`）保存 `flags`、`cli`、析构时
**仅在原 IF 为 1 时** `sti` ——**嵌套安全**。

`ProcessAddressSpace::CopyToUser`（`src/memory/process_address_space.cpp:732-745`）
**先** `ValidateUserRange` 整个范围（`:734`），通过后复制循环**没有失败分支**。
所以：**范围非法 → 一个字节都不写；范围合法 → 全部写入。**

### 2.2 事务顺序

| 步 | 动作 | 保护 |
|---|---|---|
| 1 | 校验 wire size 精确匹配 | 外层 `InterruptGuard` 内 |
| 2 | `CopyFromUser` **一次快照整个请求** | 同上 |
| 3 | 校验版本 → 字段 → owner → 句柄 → 生命周期 | 同上 |
| 4 | 选出最多 32 条的快照到固定 512 字节 bank scratch | 同上 |
| 5 | 一次 `CopyToUser(destination, scratch, n*16)` | 同上（内部嵌套 Guard，安全） |
| 6a | 成功 → 批量消费那 n 条 → 返回 n | 同上 |
| 6b | 失败 → **不消费任何选中项**（含控制槽）→ `BAD_ADDRESS` | 同上 |

整个事务期间对象与队列稳定：**不解除保护、不调用 GUI、不调度、不等待**。

### 2.3 边界

| 情形 | 返回 |
|---|---|
| `count == 0` | `BAD_SIZE`（第一版只接受 1..32） |
| `count > 32` | `BAD_SIZE` |
| 空队列 | `0`，**不复制任何记录** |
| 坏请求指针 | `BAD_ADDRESS`（第 2 步） |
| 坏目的地 | `BAD_ADDRESS`，队列不消费（第 6b 步） |
| 请求与输出重叠 | 按第 2 步的快照处理，**输出后不重读 request** |

坏请求指针与坏目的地是**两条不同路径**，错误码相同但发生位置不同。

**诚实边界（已按实现修正）**：`CopyToUser` 在范围非法时**不写任何字节**，
范围合法时**完整写入**——不存在"部分写"的中间态。因此失败时目的地与队列**都未变**。
（r2 写的"目的地可能已部分变化"对本实现**不成立**，此处更正。）

---

## §3 输入丢失必须通知应用：RESET 记录

### 3.1 为什么不能只复位 Desktop

应用已读到 key-down、后来的 key-up 被丢弃时，桌面清自己的 `keys[]` **不会改变应用状态**；
焦点从 A 转到 B 时，抬起事件送给 B 会让 A 永久卡住。所以必须有**应用可见的恢复协议**。

### 3.2 事件类型（7 种）

| type | 含义 | code | x / y |
|---|---|---|---|
| 1 | 键按下 | 映射后的 8 位 key 码 | 0 |
| 2 | 键抬起 | 映射后的 8 位 key 码 | 0 |
| 3 | 指针按下 | 按钮号（**仅 1**） | 客户内容相对 |
| 4 | 指针抬起 | 按钮号（**仅 1**） | 客户内容相对 |
| 5 | 指针移动 | 0 | 客户内容相对 |
| 6 | CLOSE | 0 | 0 |
| **7** | **RESET** | 0 | 0 |

应用收到 `RESET` 后**清空全部按键与按钮状态**。收到 `CLOSE` 同样要求取消全部输入状态。
不提供时间戳、不提供原始 scancode、不提供左键以外的按钮。

### 3.3 队列结构：128 项固定存储

| 区 | 容量 | 说明 |
|---|---|---|
| 普通环 | **126 项** | 键/指针事件 |
| RESET 槽 | 1 项 | 独立槽 |
| CLOSE 槽 | 1 项 | 独立槽 |
| **合计** | **128 项** | 每项 16 字节 = 2048 字节 |

**普通容量是 126，不是 128。** 不再有"128 条普通事件"或"全是 close 的 128 项队列"这种
情形。

### 3.4 溢出：重置而非驱逐

普通环满时：

1. **丢弃全部未读普通项**，其条数饱和累加进 `dropped`；
2. 置 **RESET pending**；
3. 再排入当前新普通项。

规则：

| 规则 | 内容 |
|---|---|
| RESET 永不丢弃 | 重复溢出**合并**尚未读的 RESET，绝不丢 |
| 读取顺序 | `RESET` **先于**随后排入的普通记录 |
| 目的 | 避免 RESET 之后重放缺口之前的旧按下记录 |
| `dropped` | 只计未读普通项的丢弃数，**饱和**在 `UINT32_MAX`，不回绕 |
| 控制事件 | **不计**丢弃，不靠轮询 `dropped` 恢复状态 |

### 3.5 其它触发同一重置方法

| 触发 | 行为 |
|---|---|
| 焦点从 A 转到 B | 对 A 的普通队列做同一重置（排 RESET），清 A 的 capture |
| 旧 IRQ 环 `overflow`（`modern_desktop.cpp:830`） | 对受影响的 native 窗口做同一重置 |
| 首次桌面 `RequestClose` | 清普通队列与 RESET（丢弃项计入 `dropped`），进 `closing`，只保留一次 CLOSE |

这些都在**延后普通路径**执行，**不把服务访问加回 IRQ**。

### 3.6 INPUT_READ 选择规则

| 生命周期 | 读取内容 |
|---|---|
| `live` | **先 RESET**（若 pending），再按 FIFO 读普通项 |
| `closing` | **只读 CLOSE** |
| `dead` | `BAD_HANDLE` |

CLOSE 读后清 pending，但"已发出关闭请求"由**生命周期**保留，不会再次排入。

---

## §4 GUI：生命周期同步与路由优先级

### 4.1 生命周期怎样到达桌面

服务/系统调用**只改固定对象数据**，**不在 bank 保护内回调 WM 或绘制**。

Desktop 在 `Update` 的普通路径通过**固定 provider 接口**枚举至多 4 个
`live`/`closing` 槽的元数据：

```
struct NativeSlotView {
    unsigned handle;            // 0 = 空槽
    unsigned width, height;     // CREATE 时确定
    unsigned frameGeneration;   // 0 = 尚未发布
    unsigned lifecycle;         // 0 live / 1 closing / 2 dead
};
// 仅 index 越界返回 false 且 view/out 均不变；
// 0..3 一律返回 true 并完整写 view 与 out。
bool CopySlot(unsigned index, NativeSlotView& view,
              gui::NativeImageSnapshot& out) const;
```

参数：`index` 0..3。**返回值语义**：

| 情形 | 返回 | `view` / `out` |
|---|---|---|
| `index > 3` | **`false`** | 两者**都不变** |
| 空槽 | **`true`** | `view.handle = 0`、`lifecycle = dead`、其余清零；`out` 全零 |
| 活对象、`frameGeneration == 0` | **`true`** | 当前元数据；`out` 为**零帧**，`width/height` 取 CREATE 尺寸 |
| 活对象、已发布 | **`true`** | 当前元数据；`out` 为 `front` 副本 |

**空槽与无更新不再混为一谈**：两者都返回 `true`，由 `view.handle` 区分（`0` = 空槽）。
第一版每次复制至多 4 个 4 KiB 槽即可，**不造差分/已知 generation 协议**。
Desktop 按 `view.handle` 识别新建/销毁，按 `view.frameGeneration` 识别内容更新——
**两者不能混为一个身份**。

保护边界：provider 内部自持 `InterruptGuard` 完成复制，**返回后不保留任何 front 裸指针**。

绑定规则：

| 情形 | 行为 |
|---|---|
| 新 handle（含 `frameGeneration == 0`） | 绑定独立槽，`Open` **空内容窗口** |
| 后续 PRESENT | **只更新帧**，不重开窗口、不抢焦点 |
| `closing` | 保留 WM 与 front |
| `dead` 或槽换 handle | 清旧快照、capture、绑定 → `wm.Close` 旧窗 → 接新对象 |
| 销毁后 | 句柄立即无效；**物理屏幕在下一次桌面同步/重绘移除**，不承诺系统调用内同步画屏 |

**缓存帧不是对象存活的证据。** 绘制与投递都必须用当前绑定的有效句柄门校验。
provider **不给悬空的 front 裸指针**。

### 4.2 路由优先级：按事件类型分支，不共用一条 Hit→Focus

| 事件 | 顺序 |
|---|---|
| **键** | 直接送**当前焦点**；焦点是 native kind 则写队列。不改变焦点 |
| **无 capture 的指针 move/up** | **不改变焦点**；仅当命中窗口且点在客户内容内才送该窗口 |
| **指针 down** | ① 桌面先处理标题栏/按钮/边框（`Title = 38`，桌面消费）② 合法命中 `wm.Hit` ③ 按既有规则 `wm.Focus` ④ **只有客户内容内 down 才建立 native capture** |
| **有 capture 的 move/up** | **优先送原 handle**，不按当前 Hit 重定向；允许**有符号越界**坐标；`up` 后清 capture |
| **无 capture 时客户内容外** | **不送应用** |
| 焦点变化 | 通知**旧** native 窗口 RESET |
| 销毁 | 清 capture；**不向已销毁对象排 CLOSE** |

### 4.3 坐标与矩形：一份规则

客户内容矩形与 `modern_render.cpp:401-438` `DrawNativeImage` 一致：

```
availableWidth  = r.w - 48
availableHeight = r.h - 124
scale = min(availableWidth / image.width, availableHeight / image.height)
scale = min(scale, 6)
若 scale <= 0 → 不绘制，也不做任何除法（此时不投递输入）
width  = image.width  * scale
height = image.height * scale
imageX = r.x + (r.w - width) / 2
imageY = r.y + 82 + (availableHeight - height) / 2
```

- **客户内容矩形** = `(imageX, imageY, width, height)`。
- `frameGeneration == 0` 时**仍按 CREATE 尺寸**计算矩形（窗口存在、内容空白）。
- 逆变换：`clientX = (windowX - imageX) / scale`，`clientY = (windowY - imageY) / scale`。
- `Hit == -1` 且**无 capture** 时：键仍送当前焦点；指针事件丢弃。
- `Hit == -1` 但**有 capture** 时：`move`/`up` **仍送原有效 handle**（capture 优先于 Hit），
  `up` 后再清 capture。capture 不因 Hit 落空而失效。
- **Draw 与坐标转换使用同一个矩形计算函数**，不写两份公式。

---

## §5 状态与内存

### 5.1 权威状态表（由 §1 推导）

| 生命周期 | `progress` | `frame_generation` | 可 SUBMIT | 可 PRESENT | 可 INPUT_READ | 派生显示 |
|---|---|---|---|---|---|---|
| — (dead/未创建) | — | — | 否 | 否 | 否 | — |
| `live` | `0` | `0` | 是 | 否 (`BAD_STATE`) | 是 | `EMPTY` |
| `live` | `>0` 且 `< total` | 任意 | 是 | 否 (`BAD_STATE`) | 是 | `DRAFTING` |
| `live` | `== total` | 任意 | 否 (`BAD_STATE`) | **是** | 是 | `DRAFTING` |
| `live` | `0` | `>0` | 是 | 否 (`BAD_STATE`) | 是 | `PUBLISHED` |
| `closing` | 任意 | 任意 | 否 (`BAD_STATE`) | 否 (`BAD_STATE`) | 是（仅 CLOSE） | 保留 |

### 5.2 内存表（含 Desktop 缓存）

| 项 | 单份 | 4 份 |
|---|---|---|
| bank `front` | 4096 | 16384 |
| bank `draft` | 4096 | 16384 |
| bank 队列（128 × 16） | 2048 | 8192 |
| bank 读取 scratch | — | 512 |
| Desktop `NativeImageSnapshot`（`native_image.h` 实测 **4108**） | 4108 | **16432** |
| 元数据（句柄/尺寸/generation/生命周期/progress/dropped/pending） | 约 64 | 约 256 |
| **合计** | | **58160 字节 ≈ 56.8 KiB** |

r2 的 40.2 KiB 只算了 bank，**漏掉 Desktop 至少 16 KiB 像素缓存**。全部为静态存储，
无堆分配，**整帧不放在内核小栈上**。

---

## §6 Wire（6 调用，`0x4750..0x4755`）

已占用 50 个：`0x4700..0x4717`、`0x4720..0x472f`、`0x4730..0x4732`、`0x4740..0x4746`。
`0x4750..0x4755` 未占用；`0x4756` 起保留。

```c
#ifndef GTOS_PROCESS_WINDOW_ABI_H
#define GTOS_PROCESS_WINDOW_ABI_H
// PROPOSED - not implemented. int 0x80: EAX=call, EBX=request, ECX=exact bytes.

#define GTOS_WINDOW_ABI_VERSION   1U
#define GTOS_SYS_WINDOW_CREATE    0x4750U
#define GTOS_SYS_WINDOW_SUBMIT    0x4751U
#define GTOS_SYS_WINDOW_PRESENT   0x4752U
#define GTOS_SYS_WINDOW_CLOSE     0x4753U
#define GTOS_SYS_WINDOW_INFO      0x4754U
#define GTOS_SYS_INPUT_READ       0x4755U

#define GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED 1U
#define GTOS_WINDOW_MAX_WIDTH      32U
#define GTOS_WINDOW_MAX_HEIGHT     32U
#define GTOS_WINDOW_MAX_BYTES      4096U
#define GTOS_WINDOW_WRITE_LIMIT    256U
#define GTOS_WINDOW_MAX_OBJECTS    4U
#define GTOS_WINDOW_MAX_PER_OWNER  1U
#define GTOS_INPUT_STORAGE_ENTRIES 128U
#define GTOS_INPUT_NORMAL_ENTRIES  126U
#define GTOS_INPUT_READ_LIMIT      32U
#define GTOS_INPUT_EVENT_BYTES     16U
#define GTOS_INPUT_READ_SCRATCH    512U

#define GTOS_WINDOW_CREATE_REQUEST_BYTES   20U
#define GTOS_WINDOW_SUBMIT_REQUEST_BYTES   20U
#define GTOS_WINDOW_CONTROL_REQUEST_BYTES   8U
#define GTOS_WINDOW_INFO_REQUEST_BYTES     12U
#define GTOS_INPUT_READ_REQUEST_BYTES      16U
#define GTOS_WINDOW_INFO_RESPONSE_BYTES    40U

/* INFO display enum - derived, never an operation condition */
#define GTOS_WINDOW_SHOW_EMPTY      0U
#define GTOS_WINDOW_SHOW_DRAFTING   1U
#define GTOS_WINDOW_SHOW_PUBLISHED  2U

/* lifecycle */
#define GTOS_WINDOW_LIVE            0U
#define GTOS_WINDOW_CLOSING         1U
#define GTOS_WINDOW_DEAD            2U

/* input tags */
#define GTOS_INPUT_KEY_DOWN      1U
#define GTOS_INPUT_KEY_UP        2U
#define GTOS_INPUT_POINTER_DOWN  3U
#define GTOS_INPUT_POINTER_UP    4U
#define GTOS_INPUT_POINTER_MOVE  5U
#define GTOS_INPUT_CLOSE         6U
#define GTOS_INPUT_RESET         7U

#define GTOS_WINDOW_ERR_TOO_LARGE           (-7)
#define GTOS_WINDOW_ERR_BAD_HANDLE          (-9)
#define GTOS_WINDOW_ERR_BAD_ADDRESS        (-14)
#define GTOS_WINDOW_ERR_BAD_STATE          (-22)
#define GTOS_WINDOW_ERR_BAD_SIZE           (-22)
#define GTOS_WINDOW_ERR_LIMIT              (-24)
#define GTOS_WINDOW_ERR_RANGE              (-34)
#define GTOS_WINDOW_ERR_UNSUPPORTED_VERSION (-38)
#define GTOS_WINDOW_ERR_UNSUPPORTED_FORMAT  (-38)
#define GTOS_WINDOW_ERR_OVERFLOW           (-75)
#define GTOS_WINDOW_ERR_BAD_PIXELS         (-84)
#define GTOS_WINDOW_ERR_HANDLE_EXHAUSTED   (-75)

typedef struct GtosWindowCreateRequest {
    unsigned version, width, height, stride, format;
} GtosWindowCreateRequest;                       /* 20 */

typedef struct GtosWindowSubmitRequest {
    unsigned version, handle, offset, source, length;
} GtosWindowSubmitRequest;                       /* 20 */

typedef struct GtosWindowControlRequest {
    unsigned version, handle;
} GtosWindowControlRequest;                      /* 8 */

typedef struct GtosWindowInfoRequest {
    unsigned version, handle, result_va;
} GtosWindowInfoRequest;                         /* 12 */

typedef struct GtosInputReadRequest {
    unsigned version, handle, count, destination;
} GtosInputReadRequest;                          /* 16 */

typedef struct GtosWindowInfoResponse {
    unsigned handle, lifecycle, show, width, height;
    unsigned frame_generation, progress, total, queue_depth, dropped;
} GtosWindowInfoResponse;                        /* 40 */

typedef struct GtosInputEvent {
    unsigned type;    /* GTOS_INPUT_* */
    unsigned code;    /* mapped key code, or button (1 only) */
    int      x;       /* client-content relative, signed 32-bit */
    int      y;       /* client-content relative, signed 32-bit */
} GtosInputEvent;                                /* 16 */

#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "32-bit unsigned");
static_assert(sizeof(int) == 4, "32-bit int");
static_assert(sizeof(GtosWindowCreateRequest)  == GTOS_WINDOW_CREATE_REQUEST_BYTES,  "create ABI");
static_assert(sizeof(GtosWindowSubmitRequest)  == GTOS_WINDOW_SUBMIT_REQUEST_BYTES,  "submit ABI");
static_assert(sizeof(GtosWindowControlRequest) == GTOS_WINDOW_CONTROL_REQUEST_BYTES, "control ABI");
static_assert(sizeof(GtosWindowInfoRequest)    == GTOS_WINDOW_INFO_REQUEST_BYTES,    "info ABI");
static_assert(sizeof(GtosInputReadRequest)     == GTOS_INPUT_READ_REQUEST_BYTES,     "input read ABI");
static_assert(sizeof(GtosWindowInfoResponse)   == GTOS_WINDOW_INFO_RESPONSE_BYTES,   "info response ABI");
static_assert(sizeof(GtosInputEvent)           == GTOS_INPUT_EVENT_BYTES,            "input event ABI");
static_assert(__builtin_offsetof(GtosInputEvent, x) == 8, "event x offset");
static_assert(__builtin_offsetof(GtosInputEvent, y) == 12, "event y offset");
static_assert(__builtin_offsetof(GtosWindowInfoResponse, frame_generation) == 20, "info generation offset");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(unsigned) == 4, "32-bit unsigned");
_Static_assert(sizeof(int) == 4, "32-bit int");
_Static_assert(sizeof(GtosWindowCreateRequest)  == GTOS_WINDOW_CREATE_REQUEST_BYTES,  "create ABI");
_Static_assert(sizeof(GtosWindowSubmitRequest)  == GTOS_WINDOW_SUBMIT_REQUEST_BYTES,  "submit ABI");
_Static_assert(sizeof(GtosWindowControlRequest) == GTOS_WINDOW_CONTROL_REQUEST_BYTES, "control ABI");
_Static_assert(sizeof(GtosWindowInfoRequest)    == GTOS_WINDOW_INFO_REQUEST_BYTES,    "info ABI");
_Static_assert(sizeof(GtosInputReadRequest)     == GTOS_INPUT_READ_REQUEST_BYTES,     "input read ABI");
_Static_assert(sizeof(GtosWindowInfoResponse)   == GTOS_WINDOW_INFO_RESPONSE_BYTES,   "info response ABI");
_Static_assert(sizeof(GtosInputEvent)           == GTOS_INPUT_EVENT_BYTES,            "input event ABI");
_Static_assert(__builtin_offsetof(GtosInputEvent, x) == 8, "event x offset");
_Static_assert(__builtin_offsetof(GtosInputEvent, y) == 12, "event y offset");
#endif
#endif
```

所有 `sizeof` 断言**直接与对应的 `*_BYTES` 宏比较**（不写字面量），所以宏与结构不可能各自漂移。
C 分支的 offset 也使用 `__builtin_offsetof`（clang/gcc 内建），不引用未声明的 `offsetof`。

`x`/`y` 是**显式 `int`（32 位有符号）**。所有请求先校验 wire size、再 `CopyFromUser`
快照、再校验版本与字段、最后 `CopyToUser` 响应。

---

## §7 调用序列

§7.1–§7.7 是**由规则推导的纸面预期**，不是实测记录。哪些分支已经在真实 guest 中执行、
哪些尚未执行，见 §7.8；§7.8 是唯一可以当作"已发生"的证据来源。

### 7.1 创建 → 两帧 → 重复 PRESENT

| 步 | 操作前 | 调用 | 返回 | 结果 |
|---|---|---|---|---|
| 1 | — | `CREATE {1,32,32,128,1}` | 句柄 7 | live, progress 0, gen 0, 显示 EMPTY |
| 2 | live/p0/gen0 | 16× `SUBMIT {1,7,offset,buf,256}` | 各 256 | progress 4096 = total，显示 **DRAFTING** |
| 3 | progress==total | `PRESENT {1,7}` | 0 | front=新帧, gen 1, progress 0, 显示 PUBLISHED |
| 4 | live/p0/gen1 | `PRESENT {1,7}` | **BAD_STATE** | front 不变, gen 仍 1 |
| 5 | live/p0/gen1 | 16× `SUBMIT` | 各 256 | progress 4096，DRAFTING |
| 6 | progress==total | `PRESENT {1,7}` | 0 | gen 2 |

### 7.2 非连续 offset 被拒

| 步 | 操作前 | 调用 | 返回 | 结果 |
|---|---|---|---|---|
| 1 | progress 256 | `SUBMIT {1,7,0,buf,256}` | **BAD_STATE** | progress 仍 256，draft 不变 |
| 2 | progress 256 | `SUBMIT {1,7,256,buf,256}` | 256 | progress 512 |

（第一版拒绝重启；要重来请销毁重建。）

### 7.3 请求关闭 vs 销毁

| 步 | 操作前 | 操作 | 返回 | 结果 |
|---|---|---|---|---|
| 1 | live | 桌面 `RequestClose(7)` | — | → closing，排 1 条 CLOSE |
| 2 | closing | 桌面 `RequestClose(7)` | — | **无新事件**（幂等） |
| 3 | closing | `INPUT_READ {1,7,32,dest}` | 1 | 读到 CLOSE，清 pending |
| 4 | closing | 桌面 `RequestClose(7)` | — | **仍无新事件**（生命周期保留） |
| 5 | closing | `SUBMIT {1,7,...}` | `BAD_STATE` | 最后帧仍可见 |
| 6 | closing | `WINDOW_CLOSE {1,7}` | 0 | **销毁**，句柄撤销 |
| 7 | dead | `WINDOW_CLOSE {1,7}` | `BAD_HANDLE` | — |
| 8 | dead | `SUBMIT {1,7,...}` | `BAD_HANDLE` | — |

owner 回收走同一 destroy 终态（句柄撤销、帧不可见、队列释放、不投 CLOSE）。

### 7.4 输入批次与坏目的地

| 步 | 操作前 | 调用 | 返回 | 结果 |
|---|---|---|---|---|
| 1 | 队列 5 条（含 1 RESET pending + 4 普通） | `INPUT_READ {1,7,32,dest}` | 5 | 写入 5 条，RESET 在首位，消费 5 条 |
| 2 | 队列 3 条 | `INPUT_READ {1,7,32,坏地址}` | `BAD_ADDRESS` | **3 条全部不消费**，dest 未写 |
| 3 | 队列空 | `INPUT_READ {1,7,32,dest}` | 0 | 不复制任何记录 |
| 4 | — | `INPUT_READ {1,7,0,dest}` | `BAD_SIZE` | — |
| 5 | — | `INPUT_READ {1,7,33,dest}` | `BAD_SIZE` | — |
| 6 | 请求与 dest 重叠 | `INPUT_READ` | 正常 | 使用第 2 步的快照，不重读 request |

### 7.5 溢出与 RESET

| 步 | 操作前 | 操作 | 结果 |
|---|---|---|---|
| 1 | 已读 1 条 down，队列满 126 | 新事件到达 | 丢弃 126 未读项，`dropped += 126`，RESET pending，排入新项 |
| 2 | RESET pending | 再次溢出 | RESET **合并**（仍只 1 条），不丢 |
| 3 | — | `INPUT_READ` | **先读到 RESET**，再读溢出后的普通项 |
| 4 | 焦点 A→B | — | A 收到 RESET，A 的 capture 清除 |

### 7.6 capture 与销毁

| 步 | 操作前 | 操作 | 结果 |
|---|---|---|---|
| 1 | 客户内容内 down | — | 建立 capture，记 handle 7 |
| 2 | 有 capture | `PRESENT` 新帧 | **handle 不变**，capture 仍有效 |
| 3 | 有 capture | 移出窗口 move/up | **仍投 handle 7**，坐标可越界 |
| 4 | up 后 | — | capture 清除 |
| 5 | 销毁 7，槽重用给 handle 9 | 旧 handle 7 的任何调用 | `BAD_HANDLE`，**不得路由到 9** |

### 7.7 桌面可见性

| 步 | 操作前 | 操作 | 结果 |
|---|---|---|---|
| 1 | live | 桌面 `RequestClose` | 最后帧**保持可见** |
| 2 | closing | 下次 `Update` | 窗口仍在，帧不变 |
| 3 | 应用 `CLOSE` | — | 下次 `Update` 隐藏并释放 GUI 绑定 |
| 4 | 同时 | 旧 `ModernImage` | **继续独立工作**，不受影响 |

### 7.8 实测覆盖：probe 阶段、VM 目的地矩阵、四模块 ISO、回收两阶段

本节只记录**真实执行过**的分支。`CHECK` 的静态个数不是运行案例数，二者不互相代替。

#### 7.8.1 分段 probe 协议（必需位门）

`apps/native_window_probe/native_main.cc` 按固定顺序推进，每段自己声明完成：

| 阶段 | 日志标记 | 含义 |
|---|---|---|
| 启动 | `GTOS NATIVE WINDOW START V1` | 进入 ABI 路径 |
| 就绪 | `GTOS NATIVE WINDOW READY V1` | 帧已发布，等待外部输入 |
| 1 | `STAGE INPUT_COPY_DONE V1 <n>` | 非空队列下的目的地拒绝矩阵完成 |
| 2 | `STAGE RESET_COPY_DONE V1 <n>` | RESET pending 拒绝后再恰好消费一次 |
| 3 | `STAGE CLOSE_COPY_DONE V1 <n>` | CLOSE pending 拒绝后再恰好消费一次 |
| 通过 | `GTOS NATIVE WINDOW PASS CREATE SUBMIT PRESENT INPUT CLOSE V1` | 仅当 `requiredMask` 覆盖全部段 |

规则：**必需段没有到达或没有执行，本轮就是失败**。不允许用"可选 `if`"跳过负例，
也不允许把静态 `CHECK` 数量当作运行案例数。runner 按阶段标记驱动 QMP，前一段没完成
就不发下一段输入。

#### 7.8.2 VM 目的地矩阵（用真实页权限，不改 ABI）

用既有 `include/process/vm_abi.h` 建立页面并证明权限，不借宿主映射。**实际保留五页**，
不是两页：

| 页 | 权限 | 作用 |
|---|---|---|
| 0 | READ_WRITE | 可写端 |
| 1 | 未映射（`DECOMMIT` 后） | (b) 的跨页空洞 |
| 2 | READ → 之后升 READ_WRITE | (a) 的只读目的地；(d) 在同一地址/长度上差分 |
| 3 | READ_WRITE | (c) 目标起始页，可写端是该页末尾 8 B |
| 4 | READ（预填哨兵 `0xa5`） | (c) 目标的只读部分 |

| 案例 | 目的地 | 期望 | 证明点 |
|---|---|---|---|
| (a) | 只读页（页 2） | `BAD_ADDRESS` | 队列不消费 |
| (b) | 跨到未映射空洞（页 1） | `BAD_ADDRESS` | 按**实际将复制的事件数 × 16** 越界，不拿 `count` 上限代替 |
| (c) | 从页 3 末尾 8 B 起，长度 `multi × 16`，跨入页 4 | `BAD_ADDRESS` | **整个目标逐字节未改**：可写端 8 B + 只读段 `multi × 16 - 8` B |
| (d) | 页 2 同地址、`count = 1`、`SET_PERMISSIONS` 升为读写 | 成功 | 与 **(a)** 同地址同长度构成差分，证明拒绝来自页权限 |
| (e) | 请求与输出同一 buffer | 成功 | 事件覆盖该 request，且**恰消费一次** |
| (f) | 带哨兵的目的地 | 成功 | 只写 `n×16` 字节，哨兵不变 |
| (g) | 空队列 | `0` | **不复制**，因此空队列上坏目的地不可观测 |

关于 (c) 的实测范围：早期版本只核对只读页头 16 B，`multi ≥ 3` 时剩余目标字节没有检查，
因此**不能**写成"整个目标一字节未改"。现版本用实际目标长度算出只读段
`multi × GTOS_INPUT_EVENT_BYTES - 8` 并逐字节核对。本轮实测日志
`GTOS NATIVE WINDOW CROSS READONLY V1 48 8 40`：目标 48 B = 可写端 8 B + 只读段 40 B，
两段都核过。

关于 (d) 的范围：它升权的是**页 2**、`count = 1`，对应 (a) 的同地址/同长度；它**不是**
(c) 的第 4 页多记录差分。(c) 的结论是完整目标范围的拒绝及字节未变，不声称
对它做过同地址、同长度的升权差分。

#### 7.8.3 四模块 ISO 合同

`tests/native_window_iso_build.sh` 只允许**替换**固定四个槽中的一个：

```
/boot/catch.gtapp  /boot/native-fault.elf  /boot/native-peer.elf  /boot/browser-probe.elf
```

- 未知名、重复槽、含 `/` `\` 或 `..` 的名字，以及任何第 5 个模块，都在**任何复制/写入
  之前**统一拒绝；`--check-only` 可单独证明拒绝且不落盘。
- 输出目录必须全新（不只看 `GTOS.iso` 是否存在）。
- 生产 kernel 复用已审对象；诊断变体走 `obj/native-reclaim/` 独立路径，不会因旧对象复用
  进入生产镜像。
- 最终入口与仓库外依赖：guest 由 `tests/native_window_qemu.py` 驱动，构建入口是
  `tests/native_window_probe_build.sh` 与 `tests/native_window_iso_build.sh`；两者都依赖
  仓库外的已资格 PNG 缓存 `artifacts/png-release-qualified-20261007T101400Z` 与
  `toolchains/chromium-clang-llvm24-62397f8b-57`，并复用该缓存里 8 个已核摘要对象中的 6 个。
  这些依赖**不在**仓库内，摘要随每次构建重新核对，不能靠"文件在盘上"当证据。

#### 7.8.4 持窗回收：Stop 与 Reap 是两个阶段

| 阶段 | 观测点 | 要求 |
|---|---|---|
| A 运行中 | owner 仍 live | 确有 live 对象、已发布帧、未完成 draft；peer 另有一条真实路由输入 |
| B Stop 之后、Reap 之前 | `StopGroup` 已跑 | 槽已 DEAD、旧 handle `BAD_HANDLE`、`CopySlot` 读到的 view 已空 |
| C Reap 之后 | `Reap` 已跑 | 页/对象计数回到该组基线；新 owner 可 CREATE/SUBMIT/PRESENT/CLOSE；旧 handle 仍拒绝且不复活 |

B 必须在 `Reap` 之前取样，否则"Stop 已回收"与"Reap 兜底回收"无法区分。正常退出与
fault 各算独立结果，不能用应用主动 `CLOSE` 替代其中任一项。

关于 C 阶段的 scratch owner `0x5245434c`：它只证明 **bank 复用**——被回收的槽能被新
owner 正常 CREATE/SUBMIT/PRESENT/CLOSE。它**不是**一个新的 CPL3 用户进程准入，本轮没有
为这段措辞单独构建用户进程。

关于 GUI 移除的判据：不再使用"任意像素变化"。移除是**正向状态**，由两个互相独立的
部分共同证明：

1. **窗口边界状态**：kernel 在桌面下一次 `Update` 之后读真实窗口管理器，打印
   `NATIVE RECLAIM GUI OPEN SLOTS <n>`，`n = 0` 时给 `GTOS NATIVE RECLAIM GUI CLOSED V1`。
2. **屏幕状态**：原生窗口矩形内一条只含壁纸的横带（`(208,480,372,36)`，位于 Welcome
   窗口及其阴影之下、dock 之上、壁纸文字之左、指针之外）必须**逐像素等于**桌面为该行
   计算的壁纸颜色；同时两个 payload 帧都不得残留在内容矩形内。期望值由
   `tests/native_window_removal.py` 按 `modern_render.cpp` 的同一整数算式独立重算，
   不取自任何截图。

这两部分不能互相替代：只证明窗口管理器关闭而屏幕仍合成着窗口，或只证明像素而窗口仍
open，都算失败。该判据的反例（旧 fault 帧仍在、peer 帧仍在、单像素改动、客户区被清空
但窗口框仍在、窗口面板仍合成）由 `tests/native_window_removal_check.py` 在**不启动 guest**
的前提下证明。

---

## §8 Chromium 边界（候选，未评估完整要求）

| Chromium 概念 | 对应 | 状态 |
|---|---|---|
| `AcceleratedWidget` | 持久对象**句柄**（生命周期由 CREATE/CLOSE 定义） | **候选**，桥接未实现 |
| `NativeWindow` | 呈现对象 + 用户态包装 | 桥接未实现 |
| `NativeView` | 无对应（GTOS 无视图树） | **未实现** |
| `NativeEvent` | `GtosInputEvent` 值记录 | 候选，桥接未实现 |

本轮**未评估** Chromium 对事件/表面的全部要求。不保证"只需类型映射"。32×32 也仍不是
浏览器可用窗口。`IS_GTOS` 是目标身份，不等于框架开关；不引入 Aura/Ozone，不捆绑 socket。

边界必须照实写，不能被"整体全部通过"盖住：

- **原生 Chromium 从未在 GTOS 上运行过**。本轮全部证据来自原生窗口/输入/回收路径本身。
- 历史 Content M 结论仍是 `RAN_RC1`，本轮没有重新评估它。
- 32×32 / 4096 字节上限**不放宽**，因此即使本节全部通过，也**不**说明已能承载
  浏览器尺寸的 surface。

---

## §9 推荐路线（唯一）

**GTOS 原生对象 + 薄桥接，不先接 Aura/Ozone。**

备选（不选）：先接 Ozone（需窗口系统/焦点/合成器，GTOS 全缺）；先接 Aura（需视图树与
`NativeView`，无对应）；只加宏试编译（把问题藏到下一个 `#error`，禁止）。

---

## §10 本版明确不做

不放宽 32×32；不改旧 surface/快照/`imageProvider`/`ModernImage`/event ABI；
不实现 Aura/Ozone/`IS_GTOS` 分支；不捆绑 socket；不实现自动关闭超时；
不承诺原始 scancode 或左键以外按钮；不实现半帧重启。
