# IA32 原生文件 ABI

本模块通过真实 CPL3 系统调用读写已挂载的 FileStore。用户程序可打开、读取、写入、定位、关闭文件，操作目录，并查询路径或句柄信息。正常退出、页故障和取消后的文件关闭由内核延迟 Reap 执行。

正常桌面镜像尚未挂载此文件卷或附加文件服务；测试使用独立新建的 ATA 磁盘。LLVM stdio、libc++ fstream、完整 V8 Isolate、Chromium 网页浏览、视频播放与 HTML5 仍待接入。

## 接口和校验

接口定义在 `include/process/file_abi.h`。EAX 为操作号，EBX 为请求的用户虚拟地址，ECX 为请求的精确字节数；返回值为 EAX 中的有符号 32 位整数，其余通用寄存器和段寄存器保留。请求使用固定宽度字段，版本为 1，所有者取自内核确认的当前进程 ID。

| 操作号 | 操作 | 请求 |
| --- | --- | --- |
| 0x4720 | OPEN | OpenRequest，16 字节 |
| 0x4721 / 0x4722 | READ / WRITE | TransferRequest，16 字节 |
| 0x4723 | SEEK | SeekRequest，20 字节 |
| 0x4724 / 0x4725 | CLOSE / SYNC | ControlRequest，8 字节 |
| 0x4726 | TRUNCATE | TruncateRequest，12 字节 |
| 0x4727 | STAT | StatRequest，20 字节 |
| 0x4728 / 0x4729 | MKDIR / REMOVE | PathRequest，12 字节 |
| 0x472a | RENAME | RenameRequest，20 字节 |
| 0x472b | DIR_OPEN | PathRequest，12 字节 |
| 0x472c | DIR_READ | InfoRequest，16 字节 |
| 0x472d / 0x472e | DIR_REWIND / SIZE | ControlRequest，8 字节 |
| 0x472f | HANDLE_INFO | InfoRequest，16 字节 |

未附加服务或 FileStore 未挂载时，全部操作先返回 -19，既不读取用户请求，也不访问磁盘。可用时先检查请求长度、快照完整请求和版本，再检查路径、句柄及整段数据页。路径包括最后 NUL 最多 512 字节，不接受提前 NUL；UTF-8 字节原样传入。每次读写最多 4096 字节。零长度传输仍须提供用户地址范围内的地址，但无需已提交数据页。

写入先快照完整用户缓冲，再调用后端。读取先校验完整可写范围，即使最终是短读或 EOF；只复制实际成功读取的字节。路径、请求和结果允许重叠，因为输入已完成快照。验证错误不改变偏移、文件内容、句柄、输出或磁盘；真实 I/O 错误可能有部分后端效果，直接返回错误，读失败不把临时缓冲写回用户页。

Seek 使用完整有符号 64 位偏移，后端在运算前检查上游文件范围。Info 固定为 268 字节；HANDLE_INFO 的名称为空，STAT 和目录遍历返回实际名称。DIR_READ 每条返回 1，EOF 返回 0 且保持输出。打开标志、文件类型和后端保持一致，详见 ABI 头文件。

## 挂载、所有者及回收

由受信任的 BSP 启动代码完成真实 ATA Identify 和 FileStore 挂载，创建 NativeFiles，并在 NativeRuntime Activate 前一次性 AttachFiles。服务和后端必须比运行时活得更久。系统调用在中断保护范围内使用服务对象的有界缓冲；这是 BSP 调用约束，不提供 SMP 锁。

所有者保护打开句柄。文件和目录共用现有 64 个槽位，关闭后句柄永久失效，槽位复用不能恢复陈旧句柄。路径仍属于全局文件命名空间，当前接口没有路径 ACL，也不提供 Linux/POSIX 的完整文件语义。

退出、故障或取消时只停止任务，文件留到内核 CR3 下的延迟 Reap 关闭。关闭同步失败仍释放该所有者的全部槽位，记录失败次数和本次首个错误，保留其他进程的句柄，再执行 FP 失效和地址空间回收。失败关闭不保证持久化。未挂载服务的回收没有可关闭资源，不访问磁盘，也不产生虚假的 BADF 失败。

## 已执行验证

正式源码的两种配置各执行八次冷启动：O0 / 32 MiB / 单个固件 CPU，O2 / 64 MiB / 四个固件 CPU；每个优化级分别运行写入、冷读取、未附加服务及未挂载服务。通用调度器仍只运行在 BSP。两种配置合计 16 次启动、36 次进程回收，释放 48 个文件和目录句柄。

qualified-llvm 配置使用固定 Chromium Clang 和真实默认 LLVM memcpy/memset，O0/O2 提供者对象与之前验收对象逐字一致。scalar-fixture 使用仓库现有整数编译辅助函数供普通 CI 执行相同文件 ABI 验证；它不证明 LLVM 文件提供者已经实现。

实际客体覆盖全部 16 项操作，以及错误大小/版本、内核和未映射地址、地址溢出、跨页读写、只读及 NONE 页、守卫页、4096/4097 字节边界、零长度、短读/EOF、输入输出重叠、UTF-8、跨块文件、追加、独立偏移、空洞、截断、目录遍历、64 槽耗尽及重复关闭。它从实际 ATA 文件读取并逐字核对 1108 字节 PNG；此步骤只验证磁盘读取，本探针没有解码或提交 PNG 窗口。

每轮写入含正常退出、实际页故障、取消、失败 Flush 回收及槽位复用。每次受害任务留下文件和目录，Reap 前检查资源仍保留，Reap 后逐页核对分配器基线。常驻同伴保持两个句柄，并执行 100 次外来或陈旧句柄拒绝检查。实际读写和关闭 I/O 故障被返回，读缓冲保持不变；回收失败不会留下槽位。冷读取与未修改上游 LittleFS 的独立只读主机验证均不改变磁盘；卷前后哨兵扇区保持不变。

自审重读全部 16 份日志，拒绝 1508 个删除、重复、字段破坏及伪造通过标记。O0/O2 各把 x87、SSE、MMX、AVX 和 MXCSR 指令注入真实 NativeFiles::Call，十个控制均被正常内核审计拒绝；核对拒绝地址位于该函数内，没有新增审计豁免。真实陷阱栈观测上限为 O0 3804 字节、O2 2576 字节，底部哨兵未变化；这不是所有调用路径的静态栈证明。

新增 HandleInfo 后，完整 FileStore 后端重新执行 174 次 ATA 冷启动和 84 个扇区中断边界。正常内核 O0/O2 全量编译和整数 ISA 审计通过；原有进程、VM、身份及 PNG 显示/关闭/回收另行回归。进程、页、区域、堆及栈容量没有改变。

## 复现

使用新的绝对输出目录；旧日志、磁盘、失败记录和缓存均保留。普通配置：

```sh
python3 tools/build-native-file-probe.py /absolute/new-output --cxx /path/to/g++ --cc /path/to/gcc
python3 tools/self-review-native-files.py /absolute/new-review /absolute/new-output
```

实际 LLVM 提供者配置须同时指定固定编译器和固定 libc++ 源码：

```sh
python3 tools/build-native-file-probe.py /absolute/new-llvm-output --cxx /path/to/qualified/g++ --cc /path/to/gcc --clang /path/to/qualified/clang --libcxx-source /path/to/pinned/libcxx
```

相同工具链可以追加 `--build-only`，核对 source-lock 中已执行组件对象、用户 ELF 与实际加载的内核字节；它不会启动客体或报告新的客体验收。内核完整 ELF 的符号和映射路径可能随产物目录变化，因此锁另外记录原始 ELF 哈希，以加载段规范化哈希作执行字节比较。

验收记录见 [acceptance.json](acceptance.json)，来源与执行字节见 [source-lock.json](source-lock.json)。其他运行库模块的历史内核锁仍对应各自验收时的提交；本次没有将旧内核验收冒充为新运行时复测。

下一步实现真实 LLVM 文件提供者及 libc++ 文件流，并在明确的启动挂载路径接入原生程序。完整原生浏览器尚未运行。
