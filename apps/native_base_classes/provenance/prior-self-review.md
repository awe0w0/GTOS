# 真实类语义与资源回收终验

class-guest-03 正常终止为 0，status SHA256 为 903445bb3db71a55bd89d4c952a7ef2616381bcbd477d330e651861dce774961。complete、native_class_pass、输入/别名/原缓存元数据/新旧产物终验均为 true，failure 不存在。3061 项输入、5960 条别名；没有运行中的构建、测试、推送或登录。

独立类诊断在 GTOS CPL3 实际执行：32M/smp1 与 64M/smp4 各两轮正例，共四上下文。每轮 121 条断言、86 次系统调用、cs=0x23、两个真实构造函数、class stage3/result0、真实 exit stage4/exit0，无页故障，停止时 dynamic=0。strnlen 的 14 项情况覆盖未映射地址 max0、无 NUL 读到右保护页边界、七种起始偏移、末字节 NUL 和提前终止。完整 FilePath 类覆盖 21 项路径/UTF 情况和 32 轮长字符串拷贝、移动、追加及析构；保留上游词法语义和无效 UTF 的替换字符行为。

完整 LocalPersistentMemoryAllocator 实际构造 1MiB，读取真实 VM 前缀并查询 257 页，排除 malloc 回退。256 次成功分配包含定长分配和受控耗尽；类型/尺寸拒绝、重复 MakeIterable、迭代顺序/Reset/过滤、错误 from-type 不清零、成功 ChangeType 全载荷清零、IsFull/MemoryInfo 均通过。析构后旧映射句柄失效。该结论只覆盖匿名 LocalPMA，文件映射 flush 仍未实现。

32M 正例每轮 Reap 精确恢复 4724 空闲帧，64M 正例恢复 12908；任务数回到一个独立 kernel peer，FP 初始化/失效计数依次 1/1、2/2，无 FP 不变量失败，peer/PIT 继续运行。正例 ELF 2253 映像页，128 栈页、1024 运行时页，容量 3405；没有增加既有配额。观察器仍真实构造 NativeRuntime，并验证栈零页与两侧缺页边界。

错误长度负例只包装诊断 API NativeClassLength，返回 1，保留原 LLVM strnlen 和所有真实类对象。32M/smp1 两轮均在 max0 的第 4 条断言返回 57，mask0、cs0x23、两个构造、真实 exit57/stage4，无故障；停止时一页诊断映射保留至 Reap，随后每轮精确恢复 4725 帧、一个 peer 和对应 FP 上下文。成功正例来自 class-guest-02，v3 逐项复核原日志、映像、观察器完整 MD、构建/指令审查证据后复用，只重编和执行负例。

用户对象最终九单元逐字节解码 62878 字节，显式审查 i686/x87/SSE2 指令、寄存器、重定位和六处准确 VM 调用；无硬件 TLS 或对象构造数组。CS 前缀仅接受精确填充 NOP 字节。每种内核 35 个实际对象均经完整指令审查，含 14 个原始整数归档成员，无归档跳过。smp4 配置保持 BSP 调度范围，不推导通用 SMP 并发结论。

class-compile-04 复用五个已完成对象，仅补编入口、完整 SysInfo 候选和启动汇编。两份原始 UTF 转换源未改；SysInfo 的完整上游文件仅添加两个 GTOS 条件块，移除后与固定 Git 原文一致，VMAllocationGranularity 返回真实 VM ABI 页单位。SysInfo 其余原始逻辑保留，未支持的 native backend 仍保留强导入。CommandLine 头仅沿用已有 IS_GTOS 字符串分支，去掉该条件即恢复完整原文。所有新头先独立比对固定 Git ls-tree/hash-object，再允许 CXX。

对齐启动汇编沿用已验收原生启动体，仅更换独立诊断符号。入口添加真实 exit 所写的两个历史命名数据符号；该 ELF 不含原 V8 main/NativeEntry/_start。实际调用全部保留的构造函数和真实进程退出；正例复用已成功链接的完全相同 ELF 字节，未重放失败日志的命令。

旧失败保留：compile01/03 在未定源头文件前停止；link01 指出真实 exit result 数据和 SysInfo 缺口；link02 的过宽全符号重名假设被原始已验收 root 对照证明为检查器问题，三个 libc++ 条目均为旧有，所需 ABI 符号继续严格唯一并要求零强未定义；guest01 缺少既有一次性 kernel observer 的 -fno-threadsafe-statics，未添加空 guard 函数；guest02 负例预期误用宏首行 56，原始对象立即数 0x39 与完整宏结束行 57 一致，v3 修正位置定位并保留精确值/退出/回收断言。未修改测试代码或清洗日志来迁就结果。

执行栅栏持续生效：固定工具及 SHA、Python -I -B -S、最小环境、静态 argv/shell=False；全阶段无网络命名空间、Landlock ABI7、目录无执行授权，工作区只读、写入限全新输出；不继承凭据/代理，不执行宿主产物，不读其他进程，不使用内联 Python/shell 链，不删除或改写旧缓存。QQ 在上海时间 02:00–09:00 静默，本轮没有发送。

公共仓库仍为干净 dev/origin-dev 588ccefdd00b70168b3717b095e5c0c3bf624909，本轮未提交、未推送、未更新生产镜像。下一阶段先把 SysInfo 源视图及类诊断接入正式 GN 图并核对现有成员/规则，再制作中文公开 capsule、自审后正常提交/推送 dev。原 V8 root 先前四次真实启动与 28 组 JS 的验收继续按 v172/v173 范围保留；GTOS Content、原生浏览器、网页、视频和 HTML5 尚未运行，完整目标继续。
