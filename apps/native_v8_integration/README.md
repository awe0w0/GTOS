# 32 位 V8 完整原生目标集成入口

本模块把锁定的真实 V8、libplatform、Platform/Isolate/Context/Script 消费者和
GTOS 原生入口连接为 `native_embedder.elf` 目标。当前是显式启用的 IA32、单用户
线程、JIT-less 诊断配置，关闭 Wasm、ICU 和 sandbox。完整 ELF、客体 Isolate/JS、
浏览器、视频和 HTML5 尚未验收。当前阶段及首阻塞见[集成主线](../../docs/chromium-integration-plan.md)
和[机器清单](../../docs/chromium-integration-status.json)。

## 输入与复现

`overlay/`、`sdk/` 和 `c-sdk/` 保留平台覆盖、独立 C++ 配置及 LLVM C 声明闭包。
声明不等于运行服务。`payload-lock.json` 绑定包内每个文件，`source-lock.json`
绑定上游 revision、工具、Clang 资源头、Linux IA32 宿主库和实际使用的原头依赖。
源码版本仍为 Chromium 04bca2584cddb39e134fc7c16bb2886e02863d0e、
V8 be042d4462bee463c9b785701b8c6ee4576ee0a3；具体 DEPS、libc++、libc++abi、
LLVM libc、Abseil 和 zlib revision 以源码锁为准。原上游检出不被修改。

先准备锁定的源码依赖、sysroot、宿主 Python、Clang、GN、Ninja、IA32 宿主运行库
及整数 builtins，在 Linux/WSL 的 GTOS 仓库执行：

```sh
python3 -B tools/build-native-v8-integration.py /path/to/fresh-output \
  --v8-source /path/to/pinned-v8 \
  --clang /path/to/pinned-clang/bin/clang \
  --gn /path/to/pinned-gn --ninja /path/to/pinned-ninja \
  --host-i386-runtime /path/to/linux-i386-runtime/root \
  --integer-builtins /path/to/libscalar_integer.a \
  --enable-diagnostic --jobs 8 --build-timeout 7200
```

输出必须全新；失败、超时和旧缓存保留。`--prepare-only` 只生成 GN 图、完整依赖和
实际命令，默认执行完整 Ninja。输出的 `status.json`、编译数据库、链接命令和原始
日志是阶段证据；完整构建失败返回非零，驱动/准备错误不能记为 OS 根因。默认配置
不启用诊断目标，工具路径不能包含空格或 shell 元字符。

`--cache-from /path/to/retained/build` 将停止的缓存复制到新目录，再重新生成目标；
原缓存不变。相邻 `module-input-snapshot/source-lock.json` 必须存在并匹配固定版本
与工具。完成后将实际绑定的本模块原样冻结到 `module-input-snapshot/`，保存对应
构建脚本。缓存续编不是冷构建，也不是空白主机 bootstrap 验收。

## 已接入与验收边界

新增依赖由同一真实完整目标的错误驱动：实际 libc++ 流与 FILE 定义、真实诊断输出、
单用户任务 call_once、Ctype、分配式格式化、C++ ABI guard、C-only locale，以及
真实标准 FILE。旧命令、消费者和引擎参数保留，必要边界/回收验证后回放同一目标。

分配式格式化保留 double/80-bit long double 完整精度 f/e/g 路径。私有工作区避免
大整数临时副本，大型转换器使用真实堆和 RAII 回收；所测路径最大单函数帧 2236
字节，含 6000 位输出。当前强制单用户线程的私有地址空间选择官方 LLVM shared
errno 模式，避免 OOM 路径再次分配 TLS；不能据此声明多线程支持。

guard 使用固定 libc++abi 的官方 NoThreads 实现；初始化一次、嵌套、abort/retry、
递归终止、故障/取消和精确回收均已在客体验证。locale 只接入实际选中的
LLVM-libc/no_locale C-only 后端，不声明通用 POSIX 或命名 locale。第 26 次真实
链接已越过 newlocale/freelocale，三项实际对象与客体对象字节一致。

标准 FILE 直接复用已验收字节流算法。stdout/stderr 通过 GTOS 原生写入输出；
控制台读取和 fileno 明确报 ENOSYS，seek 报 ESPIPE。没有虚构的描述符表。
freopen 后返回实际正值原生文件句柄，可合法为 1/2；fclose 清理缓冲和句柄，静态
FILE 留在所属进程映像，公共与内部变量使用同一份指针存储。受影响的普通文件
回归及标准流资格见[标准流模块](../native_standard_streams/README.md)。

`native_scanf_libc` 使用固定 LLVM `sscanf.cpp` 和目标局部头覆盖，保留 float、double
及 80-bit long double 完整转换。实际 GN 对象已在 GTOS CPL3 的 32M/单 CPU、
64M/四 CPU 两次冷启动验证：每个正常任务 41 项扫描边界，合计 164 项；另测
页尾读取、OOM 后恢复、输出写故障、输入/格式读故障、扫描循环进程取消及精确
Reap，合计 14 个任务、16 次回收。
独立复审拒绝 594 个日志反例和 5 个 ISA 反例；最大单函数帧为 852 字节，不是完整
调用链上界。取消验证覆盖进程及其堆区回收，不证明取消瞬间正在使用内部临时缓冲。
该局部客体资格不代表完整 V8 或浏览器通过；公开工具重放仍单独验收。

信号、异步采样、完整线程/等待、通用 POSIX、完整 libc/流功能仍未闭合。缺失
服务应明确失败，不能用空指针或成功占位隐藏。原生进程数 4、256 用户页、32
区域、64 KiB 堆、8176 字节用户栈和 16 KiB 陷阱栈没有放大；完整 ELF 产生后再
测量实际装载与内存需求。Linux 参考运行不能计为 GTOS 引擎或浏览器验收。

第33次真实链接已接入[原生LowLevelAlloc](../native_low_level_alloc/README.md)：保留
固定Abseil分配算法，以真实GTOS页服务管理原始区域句柄。12个客体任务和14次
精确回收覆盖正常路径、溢出、重复释放、配额失败及活跃分配取消；508个日志反例
被拒绝。首次TLS初始化仍使用真实C堆。原分配器缺符号已消失，当前首错转为
StdcppWaiter所需的`std::mutex::lock()`，完整ELF仍未生成。

第34次真实链接已接入[原生同步库](../native_sync/README.md)：固定libc++ mutex与
condition_variable使用现有owner/depth和真实GTOS时钟/调度服务。12个客体任务、
648项检查及14次精确回收覆盖参数边界、超时持锁恢复、提前Post、Poke、等待取消
和时钟失败恢复；380个日志反例被拒绝。系统头文件改为完整MD记录并锁定，五个
对象字节一致。单用户任务边界保持，不能宣称跨线程通知或完整线程支持。当前
首错是`v8::base::OS::PrintError`，完整ELF仍未生成。


## 原生格式化FILE输出与同步修正

接入固定LLVM generic printf/fprintf/vprintf/vfprintf及六项V8 OS输出包装。
PrintError在真实stderr重定向后提交C缓冲及原生文件SYNC；w/w+截断打开返回之前
提交空版本，刷新已有读者缓存并保留游标。实际I/O错误返回原错误，失败打开与
异常回收均释放原生句柄。File.h及对象布局保持不变。

32M/1CPU与64M/4CPU共12个CPL3任务、12次精确回收、161842项检查及428个
日志反例通过。独立stock LittleFS两盘冷读回核对长输出、恢复后缀及PNG字节，
前后磁盘哈希不变。格式化检查共37272项；范围仍为私有单用户任务诊断配置。
实际完整链接full-link-37已越过OS::PrintError，当前首错ceilf；完整ELF、
Isolate/JS及浏览器仍未生成或运行。公有源码/测试逐字节核对，未重跑公有GN或客体。
详见[模块说明](../native_output/README.md)及[验收记录](../native_output/acceptance.json)。


## 原生基础数学提供者

新增native_numeric_libc接入固定LLVM ceilf/ceil/floor/fmod/abs；目标局部
-fmath-errno恢复库的域错误语义，旧fabs/sqrt及引擎参数保持。两组客体共12个
任务、12次精确回收，每任务921数学检查；原FILE/PNG等回归继续通过。
同一完整链接保留5160条旧对象命令及旧输入顺序，仅增加五项提供者。当前
真实链接终态及下一阻塞见[模块说明](../native_numeric/README.md)和
[验收记录](../native_numeric/acceptance.json)。完整ELF/Isolate/JS及浏览器仍未验收。


## 原生退出登记与生命周期

native_exit_libc接入真实__cxa_atexit/atexit、选择性及重复终结，正常main返回
和C exit统一处理TLS、静态/C回调、fini与C流关闭。既有TLS/DSO和FILE布局保持。
两组共14个任务、14次精确回收，7660项检查与536个日志反例通过。失败Flush
释放句柄并保留退出码，不承诺新数据提交。完整39的实际结果及下一阻塞见
[模块说明](../native_exit/README.md)及[验收记录](../native_exit/acceptance.json)。
公开字节对应检查不等于重跑公开GN/客体；完整V8/JS及浏览器仍待验收。


## 原生哈希、质数与无符号排序

native_cpp_libcxx接入固定上游hash.cpp、functional.cpp和algorithm.cpp，保留
原库配置与5167条旧对象命令。真实消费者、缺提供者链接反例、八模式CPL3
任务与精确回收的结果见[模块说明](../native_hash_runtime/README.md)和
[验收记录](../native_hash_runtime/acceptance.json)。完整40按实际结果继续；
公开字节对应不等于公开GN/客体重跑，完整V8/JS与浏览器仍待验收。


## 原生浮点文本转换

固定 LLVM libc 的 strtof/strtod/strtold 接入原 numeric 库；原生反例发现
十六进制向下舍入的增位错误，最小修正及受影响 sscanf 的 41 项回归通过。
独立位模式、页边界、四种舍入、FP 上下文、取消和资源回收见
[模块说明](../native_float_conversion/README.md)与
[验收记录](../native_float_conversion/acceptance.json)。完整41按实际首错继续。
公开字节对应不等于公开 GN/客体重跑，完整 V8/JS 和浏览器仍待验收。


## 原生 nearbyint

完整41的实际Atomics.pause消费者驱动接入固定LLVM nearbyint，IA32/SSE2
分支直接读取模式并复用原整数舍入算法。独立向量、两种FP初态、跨定时器/
syscall及实际标志破坏反例和精确回收见[模块说明](../native_nearbyint/README.md)
及[验收记录](../native_nearbyint/acceptance.json)。完整42按真实首错继续，
局部客体和公开字节对应不代表完整引擎/浏览器或公开重跑通过。

## 2026-10-14 原生虚函数 ABI 终止

完整42首错驱动接入固定pure/deleted实现，旧5174命令和次序保持。两个真实
ABI经实际诊断/abort退出，16任务精确回收。12次真实写后的返回值故障按固定
非Linux格式器映射校验EDOM33，并证明不重试已写出的诊断。完整43首错以
status实测推进；公开字节对应不代表公开GN/客体、远程CI或新stock读回。
完整V8/JS、浏览器网页、视频、HTML5仍待验收，生产镜像未改。

## 2026-10-14 原生 V8 FOpen

完整43首错驱动接入真实普通文件桥接，旧5175命令和次序保持。8任务精确
回收，1036检查和314日志反例通过；目录/路径/mode/排他/OOM，以及活跃
FILE取消/故障走实际原生通路。旧remove清理依赖失败保留，不改桥接库。
完整44首错以status实测推进；公开字节对应不代表公开GN/客体、远程CI或stock读回。
完整V8/JS、浏览器网页、视频、HTML5仍待验收，生产镜像未改。

## 2026-10-14 原生共享堆同步

真实heap加入原子锁与BSP Yield，首次reserve持锁者切换后继续运行；旧heap反例
实际报0x48000002。双配置24共享上下文、701542检查和精确回收通过，新heap的
默认FOpen另通过8任务回归。源码对应和包清单已核对，TLS/errno与V8线程仍未验收。
详见[共享堆模块](../native_thread_heap/README.md)，完整45及浏览器/视频/HTML5待验收。

## 2026-10-14 原生每线程存储与析构回收

真实 emutls 采用每线程地址表，固定 LLVM errno 接入无分配 owner cell，析构
在自己线程中执行并允许再注册。两配置74上下文、75816检查和精确回收通过，
错误共享TLS/errno负例拒绝；默认FOpen另通过8任务、8回收和314日志负例。
5173旧GN命令和原输入次序保持，三个clock/errno对象重编mode4，新context
实际加入。源码与1298文件包清单逐字节核对，生产镜像未改。
详见[每线程存储模块](../native_thread_tls/README.md)。共享同步/guard及V8线程
仍待实现，完整45未重放，原生浏览器/视频/HTML5未验收。

## 2026-10-14 真实 V8 线程桥接

固定 Thread/Semaphore 与每线程 key 在 GTOS 通过76上下文精确回收、48次Join、
默认栈和key代际复用；默认单线程失败路径也通过。FOpen stripped ELF 与既有
验收版本相同，沿用此前运行证据。新GN保留5177旧命令和最终顺序。
完整45已按真实38成员归档重放，首错为 CreateTimezoneCache，完整ELF仍缺失。
详见[原生V8线程模块](../native_v8_thread/README.md)。生产镜像未改，完整
Isolate、浏览器、视频和HTML5仍未验收。

## 2026-10-14 V8 平台核心补齐

UTC-only时区缓存、真实有界格式化及固定调度钩子在GTOS通过76上下文、91854
检查和精确回收。FOpen字节与既有验收版本相同，沿用旧运行证据。GN图未变。
完整46按真实38成员归档重放，三项缺失符号消除，首错变为isspace，ELF仍缺失。
详见[原生平台核心模块](../native_v8_platform_core/README.md)。生产镜像未改，完整
Isolate、浏览器、视频和HTML5仍未验收。

## 2026-10-14 字符分类与整数解析

固定 LLVM 的 isspace、isprint、strtol、strtoll 已接入原有运行库配置，函数体
未改。GN 新增四个对象，原5179条命令和最终顺序保持；客机76上下文、145358
项检查、48次Join及精确资源回收通过。完整47消除这四项缺失符号，首错变为
SetDataReadOnly，诊断仍非穷尽。详见[原生分类与整数模块](../native_text_integer/README.md)。
完整ELF、Isolate、浏览器、视频和HTML5尚未验收，生产镜像未改。

## 2026-10-14 静态数据只读保护

独立版本的原生只读ABI与OS::SetDataReadOnly已接入。真实FlagList::FreezeFlags
客机20上下文、532项检查、整范围拒绝、共享线程PF7、自覆盖请求和精确回收通过。
完整48消除SetDataReadOnly缺失，首错exp2，诊断未穷尽。详见
[原生静态只读模块](../native_static_readonly/README.md)。现有GN图不变，生产镜像未改；
完整ELF、Isolate、浏览器、视频和HTML5尚未验收。

## 2026-10-14 exp2 数值提供者

固定LLVM exp2及GTOS IA32极小输入舍入适配已验收：两组默认完整整数幂、
各线程全部58边界/四模式/两种种子，52048次调用、76上下文精确回收。
真实完整49首错前进到std::__gtos_isolate_diagnostic::cout，诊断未穷尽。
详见[原生exp2模块](../native_exp2/README.md)。生产镜像未改，完整ELF、
Isolate、网页、浏览器、视频和HTML5仍未完成。

## 2026-10-15 原生标准流与四上下文同步

固定 libc++ iostream、真实宽字符 File 方法及严格 UTF-8 转换已验收。
默认标准流 8 进程/8 回收；四上下文标准流 32 上下文/24 Join/8 Reap；
同步回归 12 程序/648 检查/14 回收，once 回归 16 程序/18 回收。
62 个修改头使用者及两处宏选头已审查，完整53输入和缓存终验通过，
但实际链接仍缺 OS::GetLastError 等依赖，完整 ELF 和浏览器尚未运行。
详见[原生标准流模块](../native_iostream/README.md)。四上下文为 BSP 范围，
未宣称用户态 SMP；生产镜像未改，视频和 HTML5 尚未完成。

增量 UTF-8 自审补充：非法首字节和第二字节立即 EILSEQ；8 个非法前缀、
6 个合法逐字节边界及原有生命周期/资源回收在最终 16 个标准流场景通过。
旧对象 C0 负控制在两配置精确失败，修正了把非法前缀报告为未完成的回归。
完整54保持 GN 顺序，7724 项输入和旧缓存终验通过；仍首缺 GetLastError，
完整 V8、网页浏览、视频与 HTML5 尚未运行。


## 2026-10-15 原生 OS 错误与临时文件

GetLastError、LogFileOpenMode 和 OpenTemporaryFile 已接入真实每线程 errno、
w+ 文件和立即 unlink 临时文件。默认与 BSP 四上下文控制共 24 程序、48 上下文、
24 Join、24 次 Reap；覆盖实际 ENOMEM/EMFILE、只读卷、非法标志、磁盘错误、
UD2 故障及外部取消，资源精确恢复。详见 [原生临时文件模块](../native_os_file/README.md)。
完整55保持实际 GN 顺序，仅替换两个已有提供者；三项 OS 缺失已消除。
完整 V8 ELF、Isolate、网页浏览、视频和 HTML5 仍未运行；生产镜像未更新。
当前完整链接首缺：v8::base::OS::GetSharedLibraryAddresses()；错误数量上限下的诊断尚未穷尽依赖。
