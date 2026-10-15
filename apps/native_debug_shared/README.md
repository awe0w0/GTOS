# 原生栈迹与共享映射能力边界

完整 V8 目标 full57 缺少七项系统相关符号。本模块补齐真实栈迹输出及缺失能力的
明确失败语义，完整58已生成真实原生IA32 V8 ELF，四组必要客机回归全部通过。
这是已锁定对象缓存的完整链接验收；冷构建尚未验收，原生Isolate/JS和浏览器尚未运行。

## 实际行为

- 栈迹默认构造记录 IA32 的真实直接调用者返回 PC，仅一帧。目标省略帧指针并关闭
  展开表，因此不追踪未经验证的 EBP 链；不宣称多帧展开或地址符号化。
- 既有 common StackTrace 继续负责数组构造、62 帧上限、Addresses、析构和 ToString。
  GTOS 文件提供默认构造、真实 stderr 输出和写入 ostream 的原始地址。
- Print 在全局 stderr 已关闭并清空时设置 EBADF 并返回。正常输出经过既有真实 FILE
  后端；OutputToStream 不更改调用者的进制、宽度、精度和填充设置。
- GTOS 没有 V8 异步信号处理器，EnableInProcessStackDumping 明确返回 false；
  DisableSignalStackDump 无已安装处理器需要撤销。内核故障日志保持独立。
- 当前 VM ABI 只支持进程私有页，不提供共享对象句柄、共享物理页引用或区域命名。
  AllocateShared 返回 nullptr，SetMemoryRegionName 返回 false。FreeShared 遇到误用
  立即终止，不把私有页当成共享页释放。现有子空间共享接口同样明确失败。
  这些是缺失能力的失败边界，不代表共享内存、IPC 或浏览器已实现。

## 必要验证

实际 GN 图保留原 5196 条对象命令，只增加 GTOS 栈迹对象；完整目标顶层 1110 个
输入次序保持不变。基础归档替换平台对象并新增栈迹对象，其他 37 个成员字节保留。
GN01 已终验，provider02 两个实际 IA32 对象已终验。完整58实际链接退出0、无未定义
符号，输入/别名/旧缓存终验通过；status294c992c20a93eb465d1c8a878b4ccd1b14b68936829a76c9493a253453fcbf1。
consumers06 两份实际ELF32无未定义符号，完整M/MD及输入/别名终验通过；
status3d2d5ffa217d01a26bcecc6318ed07b47b9296820ba78cc32b138795b6680f05。
guests02 的单线程/线程版本各通过32MiB/单CPU、64MiB/四CPU：44程序、68上下文、
24次Join、44次Reap，16个非法复制及4个错误共享释放进程。全部物理页、句柄和FP
所有者恢复基线；内核ISA审计、输入及别名终验通过。status为
a363bbdee9543b5612db48fbf965fd848eab282a7e179214c0938d82fba48c0c。
用户线程仍只在BSP运行，不声明用户SMP支持。

真实 ELF SHA256为6eb32fb64cb140cb526c64ce7b73dbe20638afae25b0f1fc913c78a4ad131c48，
文件12,927,508字节。实际 PT_LOAD：RX 9,667,764字节/2361页，RW 89,928字节/22页，
总计2383页。当前装载器上限256页；本组尚未调整该上限，也未测实际装载、堆、栈或RSS。
只读测量记录elf-measurement-01.json，SHA256为95c4fe873ed5ec436b457b4d42db4614af02fd890b5db98f0b96bc2ba2fb396b。

单线程/线程消费者沿用真实 FILE、临时文件和生命周期底座，增加：

- 两个 noinline 调用点的不同真实 PC；空迹、62 帧、超限计数、复制及赋值。
- 已知地址的 ToString/ostream/stderr 字节，流格式状态和关闭 stderr 的 EBADF。
- 根地址空间共享映射失败、不同偏移、原私有页数据/权限仍可用；子空间命名及共享
  失败后仍能正常释放，防止逻辑区间泄漏。
- mode10 把一页真实私有映射交给 FreeSharedPages；内核在 UD2 后、Reap 前验证原页
  仍映射且内容不变，Reap 后核对物理页、句柄及 FP 所有者精确回收。

mode2 现在显式关闭 stdout 和 stderr，因此异常停止时仅保留一个未命名临时文件；
内核对停止时句柄数及 Reap 释放数的断言均相应为 1，没有放宽资源检查。
原 mode6–9 的四类非法复制继续保留，mode10 为新增的共享释放所有权误用。

## 保留记录及限制

provider01 完成平台对象后，因新增 stack_trace.h 尚未绑定哈希而拒绝继续。
已以固定 V8 be042d4462bee463c9b785701b8c6ee4576ee0a3 的原始 Git blob 核对该头；
provider02 重核首轮全部输入、别名和已完成命令后复用平台对象，只编译栈迹修订版。
首轮源码、失败状态和输出均保留。closed-stderr 问题由源码自审发现，首版未计为客机通过。
consumers01 因独立消费者新增地址空间接口、尚缺真实 GN 的 Abseil 包含路径而在预处理
停止；失败保留，consumers02 补齐原 GN 目录与 allocator 宏后在新目录运行。
随后一次列全990项实际依赖中的8个新增V8/libc++头，逐个核对固定上游Git原始字节。
consumers03/04暴露独立测试入口缺少真实Abseil锁及其库内闭包；使用完整目标原有
138对象建立按需静态库，全部成员字节和顺序核对通过，没有重编这些缓存对象。
consumers05已无未定义符号，仅旧测试数据区两页断言失败：实测数据段0x2678字节。
probe-v2改为核对现有内核256页总限制，内核容量不变；consumers06重核并复用归档。
guests01的mode2已通过关闭stderr及回收，随后旧文件预期遗漏fclose实际刷入的clog
字符L；保留失败磁盘。probe-v3仅修正两个内核观察器的精确预期并加入有界诊断，
19个用户源码/ABI文件与v2逐字节相同，guests02复用consumer06的同一对ELF。

执行保持 Python -I -B -S、无网络命名空间、最小环境、精确可执行文件白名单、
no_new_privs 及 Landlock 仅新输出可写。没有执行宿主目标 ELF、删除缓存、读取凭据、
修改安全配置或绕过 Git hooks。生产镜像未变。

后续以最新真实完整目标的首个失败推进。生成 ELF 后先测 PT_LOAD 大小及装载所需
元数据，再根据 GTOS 实测决定是否调整配额；不预先扩大常量来掩盖容量问题。
