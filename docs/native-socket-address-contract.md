# GTOS 用户态网络地址表示合同（012 修正版）

**状态：011 草案经 012 修正后与实现合并。** 011 草案（13945 B /
`182e097ced231f33d4a903103edf7f55be9aae48a5394252e484d0307b3986be`）**未按原样入库**，
本书替代它。011 的盘点发现成立；011 审核指出的四项合同问题已在此逐条改正，并已由本轮
实现与验证落地。

本层仍然**只是用户态地址表示**。没有 `socket()`/`connect()`/`send()`/`recv()`/
`getaddrinfo()`/`if_nametoindex()` 的声明或实现，没有新增网络调用号，没有内核 wire
合同，没有 errno 行为，没有 DNS 或接口名服务。`AF_UNSPEC`/`AF_INET`/`AF_INET6` 是本轮
**选定的用户态取值**，不是既有 GTOS 内核 socket 合同。

分层结论（六件事，不合并成一句）：

| 层 | 结论 | 依据 |
|---|---|---|
| canonical 源已存在 | 是 | SDK 已有 10 个 canonical 类型头，逐个绑定 SHA256（§3.1） |
| 布局编译通过 | 是 | i686 C11/C++ 一批静态断言通过（§5.1） |
| 真实消费者头通过 | 是 | 3 个真实 Chromium 头各自独立 TU `-fsyntax-only` 通过（§5.2） |
| M 结果 | 新前沿 | 真实 `clang -M` 越过 `sys/socket.h`，停在 `navigation_handle.h:38`（§6） |
| 转换行为 | **NOT_RUN** | `ToSockAddr`/`FromSockAddr` 函数体未编译、未执行（§7） |
| 本层用户态 socket / 内核合同 | 本轮未提供 | 无新增调用号、wire 合同或用户态传输实现（§8） |

## 1. 为什么需要这一层

Content M 的真实前沿是一行 include：

```
content/shell/app/shell_main_delegate.cc:36
  -> content/shell/browser/shell_content_browser_client.h:15
  -> content/public/browser/content_browser_client.h:51
  -> content/public/browser/process_selection_deferring_condition.h:12
  -> content/public/browser/navigation_handle.h:33
  -> net/base/ip_endpoint.h:32: fatal error: 'sys/socket.h' file not found
```

`run-10` 的一次 `clang -M`（126.831 s，`error_count = 1`）停在 `net/base/ip_endpoint.h:32`，
此前已越过 `ui/gfx` 的两条图形类型错误。

为什么走 `#else` 而不是 `IS_WIN`：真实 `build/build_config.h` 由 `__GTOS__` 派生
`OS_GTOS`，因此 `OS_POSIX` 未定义，`BUILDFLAG(IS_POSIX)()` 与 `BUILDFLAG(IS_FUCHSIA)()`
都是 0。`ip_endpoint.h:29-33` 只判 `IS_WIN`，所以第 32 行被展开。命令行里没有
`__linux__`、`__unix__`、`unix`、`_WIN32`，`-nostdinc -nostdinc++` 也在。

**头文件前沿不等于行为需求已满足**：M 只到预处理。本轮把这条前沿推过去了，`.cc` 里的
类型、`sizeof`、指针转换、长度检查仍然一次都没跑（§7）。

## 2. 011 四项合同问题的改正

### 2.1 直接复用 canonical 类型，不做同宽替换

011 草案自己写了 `socklen_t = unsigned int`。这是错的：SDK 的
`llvm-libc-types/socklen_t.h` 定义的是 `unsigned long`。i386 上两者都是 4 字节、
`sizeof` 相同，但**不是同一个 C/C++ 类型**，同一 TU 里共存会直接冲突。

本轮的做法是**只包含、不重写**：`include/net/address_types.h` 按真实名字包含 10 个
canonical 头，一个 typedef 都不重新定义。于是 typedef 拼写、struct 标签、`in6_addr` 的
匿名 union、`may_alias` 属性、头保护全部是既有的那一份。

`in6_addr` 是**含匿名 union 的 struct**；`sockaddr` 系列带 `may_alias`；
canonical `sockaddr` 的 `sa_data[1]` 使 `sizeof == 4`，**不是**宿主常见的 16。

### 2.2 真实 M 与 RAW 的来源分开

011 inventory 绑定的 RAW `build/build_config.h` 是 12702 B /
`c6e3501edea0833a2ef1b50f5cd27e151d58198af0523fa4d91ebd62a1be2039`。它**不能**为实际
GTOS 派生宏举证。run-10 实际使用的是 graph-33 视图里的版本：

```
/mnt/f/GTOS-Chromium/artifacts/native-rust-gtos-20261015-a/graph-33/source/build/build_config.h
13393 B  bd185448071dbd2907bdf2896ff754573ee841e8c4cdc939595c00c53e9a5216
```

该文件里 `#if defined(__GTOS__)` → `#define OS_GTOS 1`，并派生
`BUILDFLAG_INTERNAL_IS_GTOS()`。本轮所有 `IS_GTOS` 举证都来自这一份；RAW 12702 B 只作为
参考列出，不参与任何举证。

### 2.3 独立包含不能靠偶然顺序

`net/base/sockaddr_storage.h`（RAW 1061 B /
`f11db8dd752961a5f65d21e6477d9e3b1f29f526a5097bbc09528a87ab7ff192`）只在
`IS_WIN` 或 `IS_POSIX || IS_FUCHSIA` 下包含 `<sys/socket.h>`；GTOS 两个宏都是 0，所以它
**自己**拿不到 `sockaddr_storage`/`socklen_t`。

一个先包含 `<sys/socket.h>` 的 TU 会编译通过——但那证明的是**顺序**，不是这个头能独立
包含。所以本轮：

- RAW 头字节不动；只由受控 overlay 生成器把那一行条件改为
  `#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_GTOS)`，**仅此一处
  语义变化**；
- 消费者门用**只包含该头一个 include** 的独立 TU 编译，禁止预包含；
- 反向证据：同一 TU 对 RAW 树编译**必须失败**，且失败原因是缺 `sockaddr`/
  `sockaddr_storage`/`socklen_t`（实测 16 条 error），不是别的。
- 没有改 `ip_endpoint.h` 的生产分支来绕过 `<sys/socket.h>`。

### 2.4 边界说清

- `FromSockAddr` **先读 `sa_family`（`ip_endpoint.cc:238`），再检查传入长度**
  （`:240`/`:251`）。因此"长度 15"这类短长度案例只有在 family 那 2 字节真实可读、对齐
  正确时才是合法测试。**短长度不等于"任意无效指针也安全"**，后者是未定义行为，本轮不测、
  也不宣称。
- `SockaddrStorage` 的复制实现兼容 unique / non-unique object representation，不要求其
  trait 恒为 true。
- 最小新常量只取 `AF_UNSPEC`/`AF_INET`/`AF_INET6`。`AF_UNIX`、`addrinfo`、`SOCK_*`/
  `MSG_*`/`AI_*`、`IF_NAMESIZE`、接口名服务、传输 API 一律不进入本轮固定成员。

## 3. 已实现的用户态表示

### 3.1 canonical 类型来源（10 个头，逐个绑定）

搜索路径用的是 run-10 视图里的 `tools/gtos_native_sdk/sdk`（该路径是符号链接链，最终
物理字节落在 graph-33 视图的 SDK 里；下表 SHA256 在 `readlink -f` 后的真实文件上核对）：

| 头（`llvm-libc-types/`） | bytes | SHA256 |
|---|---:|---|
| `socklen_t.h` | 715 | `b690e8848e4ef3bcd8e1428adee44014b33eac47992f07d22d62f7dd3eb5c162` |
| `sa_family_t.h` | 778 | `e4af7bd87fd16b03bb98c80da78efe99660ff937867594e54b883797d5911ecf` |
| `in_addr_t.h` | 517 | `7e80c984d352ef90ffb49564b9a2a77393ee13591f96273ecd3f7c7e6fffc77a` |
| `in_port_t.h` | 558 | `fce4063553570a77c0f99dae2baada9128e975a2284248ed7bdaf655a6b3e5ee` |
| `struct_sockaddr.h` | 723 | `0c4099661ac5943b18eef5b75f12264c5c7cd04d37262aa3e4a5d5484d5a779e` |
| `struct_in_addr.h` | 561 | `ceffd28e45fef6d3ae5eeb5eb49eb0a56a9980f27d5702d214d04d1aaa442a22` |
| `struct_in6_addr.h` | 677 | `0ebc3ed093ee5bb490f92d4df694f7e12e7807d57aa3cd58893618178bb04a53` |
| `struct_sockaddr_in.h` | 1133 | `965edf357542ea39f0885692038c2da4a308c99a7bd5cf977c385a0d62bf8cda` |
| `struct_sockaddr_in6.h` | 1041 | `5377a9cbe49b2ad82b38b816ead821d5f68a3208acdf39bd650db70f59fddfea` |
| `struct_sockaddr_storage.h` | 860 | `053f8bd884efecfbc555e21e9d1fb75b0a7bf8b6ea384c446da04ece5e6f3558` |

连带依赖 `llvm-libc-macros/stdint-macros.h`（`2de52e163a2bf0251a5ebde3777dfa7b4cc39a3baa94b9830f4bef81b7a53f77`）
也从同一 SDK 解析，**不来自 clang resource 的 `stdint.h`**；`-H` 已证明这一点。

### 3.2 三个窄头

| logical | 仓库路径 | 作用 |
|---|---|---|
| `net/address_types.h` | `include/net/address_types.h` | GTOS 原生 i386 身份门 + 包含 10 个 canonical 头 + 3 个常量 + 头内断言 |
| `sys/socket.h` | `include/sys/socket.h` | 只 `#include <net/address_types.h>`，**不声明任何函数** |
| `netinet/in.h` | `include/netinet/in.h` | 同上 |

身份门要求 `__GTOS__` 与 `__i386__`，并**拒绝**宿主 OS 伪装：`_WIN32`、`__linux__`、
`__unix__`、`unix`、`__APPLE__`、`__FreeBSD__`，以及 `OS_LINUX`/`OS_CHROMEOS`/
`OS_ANDROID`/`OS_MAC`/`OS_WIN`/`OS_POSIX`/`OS_FUCHSIA` 与 `OS_GTOS` 并存。

常量只有三个，且与任何既有同名定义**核对值相等或明确拒绝冲突**，不静默覆盖：

| 常量 | 值 | 依据 |
|---|---:|---|
| `AF_UNSPEC` | 0 | SDK 的 Linux 宏表同值；本层自行给出，不借 `__linux__` |
| `AF_INET` | 2 | 同上 |
| `AF_INET6` | 10 | 同上 |

`llvm-libc-macros/sys-socket-macros.h` 只在 `#ifdef __linux__` 下包含
`linux/sys-socket-macros.h`，GTOS 不定义 `__linux__`（也不应该为了编译通过去定义它），
所以那份常量在 GTOS 下**取不到**；本层给的是自己的三个值，不是"Linux 宏整表"。

### 3.3 i386 实测布局（静态断言通过）

`sizeof` / `alignof` / `offsetof` 全部由 i686 编译期断言给出，不是阅读声明：

| 类型 | sizeof | alignof | 关键偏移 |
|---|---:|---:|---|
| `socklen_t` | 4 | 4 | — （类型 = `unsigned long`） |
| `sa_family_t` | 2 | 2 | — |
| `in_addr_t` | 4 | — | — |
| `in_port_t` | 2 | — | — |
| `struct in_addr` | 4 | 4 | — |
| `struct in6_addr` | 16 | 4 | `s6_addr`/`s6_addr16`/`s6_addr32` 均在 0 |
| `struct sockaddr` | **4** | 2 | `sa_family` 0、`sa_data` 2 |
| `struct sockaddr_in` | 16 | 4 | family 0、port 2、addr 4、zero 8 |
| `struct sockaddr_in6` | 28 | 4 | family 0、port 2、flowinfo 4、addr 8、scope_id 24 |
| `struct sockaddr_storage` | 128 | 4 | `ss_family` 0、`__ss_align` 124 |

`sockaddr` 是 4 字节而不是 16 字节，这一点被单独断言：按宿主头想象成 16 会让所有缓冲区
尺寸算错。

### 3.4 归属与边界

- **纯用户态兼容表示，不是 wire 格式。** 这些字节只在本进程内被 Chromium 的
  `IPEndPoint`/`SockaddrStorage` 读写；没有任何内核边界被定义，也没有字节会被送进内核或
  被内核解释。
- **host/network order**：`sin_port`/`sin6_port` 是网络序，`s_addr` 与 `s6_addr` 是网络序
  字节。转换只发生在 `base::HostToNet16`/`NetToHost16`（i386 下
  `ARCH_CPU_LITTLE_ENDIAN` 为 1，走 `std::byteswap`），不在结构定义里。
- **长度类型**：`socklen_t` 只承载"容量/已写入"这一对语义，不参与磁盘或网络编码。
- **拒绝策略**：未知 family 由 `FromSockAddr` 的 `return false` 安静拒绝；过短长度同样
  `return false`；`ToSockAddr` 容量不足时不改长度。三条都不需要内核。

## 4. 窄导出与叠加层

`tools/export-native-address-sdk.py FRESH_OUT` 只导出上面三个 logical 到
`OUT/sdk/{net/address_types.h, sys/socket.h, netinet/in.h}`，逐成员记录
checkout 与每成员 logical/target/bytes/SHA256；源路径按工具中固定 MEMBERS 映射由 checkout 推导，
manifest 没有独立 source 字段。已存在输出直接拒绝；本工具仅定义三个固定成员。

**导出的是叠加层，不是独立完整 SDK**：10 个 canonical 类型头仍由原 SDK 提供，故意不复制
（复制就会造出第二份、会漂移的 typedef，正好是本轮要避免的）。既有
`tools/export-native-graphics-sdk.py` 的默认两成员与窗口 opt-in 成员**未改**。

`tests/native_address_overlay.py REAL_SOURCE FRESH_OUT` 生成受控 Chromium overlay：核对
RAW 头与 `build/build_config.h` 的 pin，确认锚点唯一，只改那一行条件，并**证明把插入移除
后能逐字节还原原头**。原头 1061 B / `f11db8dd…`，结果 1083 B /
`549e8d5188203e741b4d5a06bf80bdb382652130d2c257429b60ef311dd68dc5`。`build_config.h`
不复制，仍从真实树解析。

## 5. 已验证的层

### 5.1 廉价层：i686 C11/C++ 布局与反例（PASS）

`tests/native_address_test.sh` 一次跑完，`NATIVE ADDRESS LAYER PASS`：

1. **阴性对照**：只用原 SDK 编译 `#include <sys/socket.h>`，命中
   `'sys/socket.h' file not found` **这一条**诊断（不是任意非零退出）。
2. **阳性**：加叠加目录后同一份字节编译通过。
3. **布局**：C++ 覆盖 §3.3 全表；C11 独立覆盖精确类型、主要结构尺寸、storage family、
   scope 与 in6_addr 联合成员的关键偏移。
4. **精确类型**：C++ 用模板同一性、C 用 `_Generic`，证明 `socklen_t` 与
   `unsigned long` 同型、与 `unsigned int` **不同型**——这是 `sizeof` 看不见的检查。
5. **两种包含顺序**：canonical→umbrella 与 umbrella→canonical 都通过，无重复定义。
6. **来源**：`-H` 证明三个名字来自本次 export、10 个 canonical 与 `stdint-macros.h`
   来自已绑定 SDK、`build_config.h` 来自真实树；无仓库 `include`、无宿主系统头。
7. **反例各命中具体门**：缺 `__GTOS__`、`__linux__` 混入、`-DAF_INET=3` 冲突、
   同宽 typedef 重定义（`typedef redefinition with different types`）、调用未声明的
   `socket()`（C++ 下 `use of undeclared identifier`）。

### 5.2 真实消费者头层（PASS）

三个真实头各自用**只包含它自己**的独立 TU，`-fsyntax-only`，`-std=c++23`（与真实
Content 编译一致；c++17 会因 `std::weak_ordering`/concepts 失败，那是 libc++ 标准问题，
不是地址问题）：

| 真实头 | 结果 | 证明什么 |
|---|---|---|
| `net/base/sockaddr_storage.h` | PASS（overlay 的 `IS_GTOS` 分支） | 该头能**自己**拿到 `sockaddr_storage`/`socklen_t` |
| `net/base/ip_address_util.h` | PASS（`<netinet/in.h>`） | `in_addr`/`in6_addr` 从窄头来，`sizeof` 4/16 |
| `net/base/ip_endpoint.h` | PASS（`<sys/socket.h>`） | 旧前沿那一行现在解析 |

同一 TU 对 RAW 树编译**失败**，16 条 error 全是缺 `sockaddr`/`sockaddr_storage`/
`socklen_t`/`AF_INET`——即"没有 overlay 就不行"，所以上面三条不是顺序侥幸。

### 5.3 这是小 TU 编译，不是 Content CXX

§5.1/§5.2 都是类型与包含层检查。它们**不运行 Chromium、不链接**，也不能替代 Content
CXX 或网络行为。

## 6. M 结果：前沿已推进

一次真实 `clang -M`（`m-run-03`，115.568 s，`error_count = 1`，`returncode = 1`，
未超时，日志 786 B）在 run-10 的只读 `source`/`build` 上运行。argv 取自
`run-10/status.json` 记录的 317 项（该文件已 pin），只做两处增补：

- 插入 `-I<本次 export>/sdk` 与 `-I<本次 overlay>`（**用 `-I` 而非 `-isystem`**：clang
  先搜 `-I`，只加 `-isystem` 会被真实树的 `net/base/sockaddr_storage.h` 遮住，overlay
  永远到不了）；
- 输出路径（`-MF`、crash、TMP）移到本轮新 OUT。

其余不变：同一 TU、同一 i686 target、全部原 owner flags、`IS_GTOS`、`-M`、无 `-MG`、
无 CXX、无链接。argv 由 `argv-diff.json` 记录为 317 → 319，插入 4 项、移除 2 项。

**新的真实首错**：

```
../source/content/public/browser/navigation_handle.h:38:10: fatal error:
'services/network/public/mojom/declarative_performance_observer.mojom-forward.h'
file not found
```

`navigation_handle.h:33` 的 `#include "net/base/ip_endpoint.h"` 现在**不再报错**——地址层
把旧的 `sys/socket.h` 阻塞点关掉了。新前沿是复用视图里缺一个**生成的 mojom-forward 头**：
该目录下有 `declarative_performance_observer.mojom-{data-view,features,module,params-data,
send-validation,shared-internal,shared.h,shared.cc}`，唯独没有 `-forward.h`；`.mojom` 源
文件在 source 树里存在。这属于 gen 视图成员缺口，不是地址问题、不是本轮接线问题，也不是
任何"新网络合同"。本轮**到此停止**，按本书不去追补 `netdb.h`/`net/if.h`/`arpa/inet.h`
或其它新网络合同，也不为此重新资格历史 producer。

## 7. 转换行为：NOT_RUN

`ToSockAddr`/`FromSockAddr` 的函数体在 `ip_endpoint.cc` 里，本轮**没有编译、没有链接、
没有执行**。因此下列行为全部 **NOT_RUN**，不得声称已通过：

- IPv4/IPv6 往返（`127.0.0.1:80`、`::1`）；
- 长度 15/16、27/28 的边界；
- `sin6_flowinfo` 恒为 0、`sin6_scope_id` 仅链路本地且非 0 才写；
- 未知 family 安静返回 false；
- `ToSockAddr` 容量不足时不改长度。

本轮只断言了这些行为**依赖的**类型与布局（§3.3），没有断言行为本身。也没有摘抄函数另写
一份，也没有提供"无条件返回 0"的替身然后称为真实消费者往返通过。

## 8. 明确的缺口与保留边界

| 缺口 | 现状 | 边界 |
|---|---|---|
| `net/if.h`（`if_nametoindex`/`if_indextoname`/`IF_NAMESIZE`） | GTOS 无；`ip_endpoint.cc:37` 在 `IS_POSIX \|\| IS_FUCHSIA` 下，本轮不展开 | `ScopeIdFromInterfaceName`/`ScopeIdToInterfaceNameValue` 一旦被编译就需要它；不能用"永远返回 0"的桩，因为 `scope_id == 0` 在 `FromValue` 里被判为无效 |
| `net/base/sys_addrinfo.h`（`netdb.h`） | GTOS 无 | `address_family.cc:11` 会需要；M 还没到那里 |
| `getaddrinfo`/`AI_*` | 无 | 名字解析，与地址表示无关 |
| 生成 mojom-forward 头缺口 | 复用视图缺 `declarative_performance_observer.mojom-forward.h` | 本轮新首错；不属于地址层，也不在本轮授权内 |
| 内核接收此布局 | **无任何证据** | 将来做 socket 调用时才需要另立 wire 合同 |
| 传输/DNS/接口名服务/`AF_UNIX` | 无 | 未实现，未声明 |

## 9. 本轮做了什么、没做什么

做了：`include/net/address_types.h`、`include/sys/socket.h`、`include/netinet/in.h`、
`tools/export-native-address-sdk.py`、`tests/native_address_overlay.py`、
`tests/native_address_origins.py`、`tests/native_address_test.sh` 与 8 个定向测试源、
一次真实 `clang -M`。

没做：没有新建 `arpa/inet.h`；没有声明或实现任何 socket 函数、errno 行为、DNS、接口名
服务；没有定义 `__linux__`/`__unix__`/`OS_POSIX`；没有引入 Aura/Ozone 分支；没有改内核、
既有 wire、RAW Chromium 头或原 SDK；没有重跑旧 mojom 链、GN/Ninja、内核/ISO/guest；
没有放宽 32×32/4096。

具体提交范围与字节快照以本轮审核批准表为准。
