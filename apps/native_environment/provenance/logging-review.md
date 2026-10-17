# 原子诊断记录修正与新目录复验

merge-guest-01已终止为1，status f08cbfb7624dadf04c20dd36ae3c69e7d7af64366772c48221ba4dd0c35f240c保留。32M完整通过；64M客体三项PASS、32 FP和精确回收通过，宿主严格解析丢一SDK项后停止，整体不计通过。

原始64M日志321f8df2...行133..135为：原CPU STOP完整；随后ECPU SAMPLE...完整采样行；下一行NV SDK case=00000005 ctor=00000001。该证据直接说明boot观察器多次outb打印期间，PIT调度peer，它在syscall IF0的CpuSyscallTap插入了一条采样；不能凭PASS静默删掉这个解析失败，也不能凭缺SDK行断言真实构造失败。

新merge-envcpu-kernel-v2.cpp只给CPU STOP、ENV SDK、CPU REAP三个原完整记录加InterruptGuard，其他语句不移入临界区。原CPU SAMPLE已经在IF0，不改；不改任何Need、用户程序、采样计数、Wait、IRQ/scheduler、FP或回收。驱动把三个新增scope及仅打印语句的四格缩进在内存中去掉后，要求与固定1d7c6443...原源逐字符相同；这是来源比较，不生成代码或命令。原根、八内核源码和两用户ELF均未变，事件观察器原源也未变。

v2使用新merge-guest-02，两观察器重新编译及完整35对象ISA，四个guest重跑。宿主完整SDK列表、计数、有理公式与所有原断言保持v1严格条件，既不合并旧行、修补原日志，也不从失败日志重放命令。额外绑定原用户startup ASM及两个ld脚本来源，以补齐只读provenance。

仍只允许固定6工具、最小-I-B-S环境、user/net无外网、Landlock>=6失败即止和新输出写权限。QEMU无NIC/磁盘/显示/monitor/serial，不在宿主执行目标。旧文件/缓存/生产镜像/公共工作树不改，QQ静默。
