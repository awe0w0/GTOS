# 原生共享堆同步

真实线程生命周期接入后，TLS首次分配会共享原有单arena堆。原分配器没有
锁，首次reserve后被抢占可让另一任务发布不同arena，使原指针失去所有者。
本模块在实际malloc/free/realloc/posix_memalign/query通路加入IA32原子锁；
获取失败使用真实Yield，让BSP上的持锁任务继续运行。EnsureArena、链表拆合、
最后块释放及VM实时query都在同一锁内。calloc只在新对象分配后清零。

realloc原来会调用公共free；加锁后这会递归等待自身。现在同一锁内使用
FreeBlock(Find(pointer))，保留移动拷贝、对齐、OOM保留旧块和零大小语义。
没有扩大64KiB堆、256页限制、线程容量或引擎参数，也没有改errno的共享后端。
本profile没有异步用户信号或分配器callback，锁为非递归。故障/外部取消
沿已验收内核路径结束整个共享进程，避免留下失去持有者的锁。

提供者使用实际GN native_runtime.heap命令逐参数匹配，保留架构/profile，
仅crash输出路径与依赖MD记录改变。完整pre-M/post-MD闭包、工具/源SHA
和全部绑定输入终验通过；没有新GN图重放或完整45重放。

同一真实消费者链接新/旧两种heap对象和实际LLVM libc errno对象，无未定义
符号。测试kernel在真实成功reserve后立即切走持锁者，结果/用户帧不伪造。
旧heap真实产生BAD_POINTER退出码0x48000002并被拒绝，新heap出现实际锁等待
Yield并继续前进。32M/smp1与64M/smp4各运行3组、每组4个共享CPL3上下文：
正常四任务交错分配/对齐/移动realloc/OOM保持/字节检查与全释放，活堆整组取消，
以及posix_memalign持锁时向未映射用户地址输出造成的真实页故障。
合计24上下文精确回收和FP初始化/退休，701542项消费者检查；每组可用帧恢复
基线，boot及ring0同伴继续。负例不计入这些正常资格数量。

新heap另用于原真实V8 FOpen消费者，保留原输入次序，仅替换heap对象。
默认关闭线程的客体再通过8任务/8精确回收、1036项检查和314个日志负控。
这核对了原FILE、OOM、取消和故障语义；不证明FILE库可被共享线程并发调用。
首轮消费者的C++20 volatile自增警告保留，改独占计数器赋值，未降低-Werror。

公开源码与私有已测试源逐字节对应，输入包清单重新核对；不宣称公开工具/
客体另行重跑、远程CI、stock冷读回或完整调用链栈界。生产镜像未更新。
TLS、per-thread errno、锁/guard和V8 Thread/Semaphore仍待实现；完整V8 ELF、
Isolate/JS、Chromium网页、视频及HTML5尚未验收。后续errno后端必须无分配，
避免持堆锁时首次TLS分配递归进入malloc。
