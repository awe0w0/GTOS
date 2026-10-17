# 事件等待自审

结论：此模块可以作为显式安装的原生事件服务提交。目标仍是完整 GTOS Chromium；
MessagePump、Mojo、窗口输入、共享页、网页显示、网络、视频和 HTML5 未由此模块验收。

## 实现检查

- 请求完整快照后才读字段，精确 wire 大小、版本、flags、数量、半范围超时和用户页
  检查先于状态改变。CREATE 的输出在不可失败的发布前写入，IF0/BSP 下无中间观察者。
- 用户 owner 来自实际 process root；worker 不使用自己的 task ID 查资源。DUP 是
  真正的引用共享，不能扩大权限或在 syscall 中传入任意 owner。
- 句柄与对象分离、正数编号不复用，完整数组先验证后选择最低就绪索引。
  自动信号只在成功 Select 时消费，manual 不消费；Set/Close 同步通知真实等待者。
- 检查与登记睡眠是同一临界区，不能丢失两步之间的 SET。WAIT 只保存内核快照和
  映射固定的 kernel stack frame，不保存用户数组或可复用对象槽指针。
- Ready/Running 超时任务不会被回写；停止、join、admit、reap 和下一 syscall
  清除旧等待。自审补充了 kernel wake 后重新 SleepTask 的路径：记录实际 sleeps
  次数，变化后旧事件登记不能消费信号、修改返回值或唤醒新的管理睡眠。
- endpoint 的 notifier 只可安装一次；回调仅允许 Select，不递归触发资源变更。
  函数指针和上下文均为可信内核设置，用户无法输入回调地址。
- Reap 在任务引用全部移除后回收 owner；有 peer 别名的对象继续存在，最后引用归零。
  无堆分配、磁盘 I/O、新页映射和宿主句柄引入；未安装 endpoint 的旧启动路径可继续工作。

## 实际证据

第一版 guest-01 已通过四组，但未检查上述 kernel 重新睡眠路径；源码、入口和回执
均保留。第二版 candidate-v2/probe-v2 增加 sleep 次数校验和独立观察器注入，以新的
guest-02 完整运行。它不是重复使用第一版产物的通过声明。

最终 status.json SHA256：
`ca8c5b0444299a53bb9b7c5a811e2760fd63d0458dcd773c14a90c522324cd0d`。
complete/native_events_CPL3_pass、输入和别名终验均为 true，failure 不存在。
源码 pin SHA256：`77793490dbad016ebab7643418294935de5e84332c73716b51855555df8428d9`。

四组 O0/O2 × 32MiB/smp1、64MiB/smp4 都观察到：

- root 124 次检查、零失败，15 次真实 TaskSleeping，80 次即时 WAIT；不增加 IRQ 或物理帧。
- 内核重新睡眠注入确实执行，旧登记未干扰新睡眠，信号留给后续成功 WAIT。
- 两 worker 的 manual/auto 唤醒、真实关闭取消、等待数组页释放后唤醒和实际 tick 回绕。
- 整组页故障：vector14/error6/address BFFFCFFC，两个 worker 此前已进入真实睡眠。
- 每组 9 个 FP 初始化和失效，不变量错误为零，peer 在两次 owner 回收后继续运行。
- O0 最终空闲帧 7062/15246，O2 为 7065/15249，分别等于各自启动前基线；句柄与对象均为零。
- 40 个 C++ 对象通过完整 M/MD 检查；95 条已记录工具动作完成，四个 QEMU 都按预期退出33。

限制：BSP 原型、64 句柄/32 事件、16 元 WaitAny，有界 PIT tick 超时。没有新增通用
SMP、无限超时 wire 值、C11 消费者验收、GTK/epoll 替代或完整 Content 运行证明。
标量编译选项和现有 FP 汇编边界保留；没有把不支持 LLD map 的旧 ISA 审计称为通过。

## 执行与发布检查

独立无网络 user/net namespace、no_new_privs、Landlock ABI7、无目录执行授权、
只允许固定 Python/Clang/LLD/QEMU/ELF loader，输出只限新 guest-02。最小环境和
Python -I -B -S 保留，构建不推送、不通知、不改源码或缓存、不更新生产镜像。
后续单独发布脚本必须复核旧规范源码基线、最终回执及每份复制摘要，再正常提交 dev。

再次只读查看最新公开 issue，当前主单仍为 #9：真实 Content 目标的第一个阻塞驱动
最小正确 OS 实现，随后必须回到同一个消费者。这次服务由实际 MessagePump/Mojo
错误引出，下一步应接入上游等待/调度链，不能继续靠叶子探针数量代替本地页面首帧。
