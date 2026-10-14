# 自审

旧全局 control.address 及全局析构链会让共享线程串用。候选采用上游控制块
索引布局和真实 thread_id owner，测试同时存活的四个上下文，不以 Linux
参考程序或单线程编译成功作为原生资格。

锁顺序复核：storage → heap → context；context 内不分配，不访问 TLS，不
执行用户回调。storage/heap 锁等待走真实 Yield，CAS acquire/store release
在实际 IA32 配置无 libatomic 或 pthread 未定义依赖。owner 槽生命周期从
首次 errno/TLS/登记到本线程正常 cleanup；清空 TLS/析构链后才复用槽。
对象模板复制和地址表增长在 storage 锁内，尺寸/对齐溢出失败走实际 abort。

析构先摘链并释放节点再调用 callback，不持 storage/context 锁，故可新建
TLS 和再次登记。全部析构完成后才释放 TLS，因此析构使用自己 TLS 有效。
禁止递归 cleanup。默认主任务整组退出只运行自己的清理；取消/故障/OOM
整组销毁私有地址空间，不承诺未正常退出的同伴析构。该语义有真实异常
case 和完整 frames/FP/task 回收证据。

负控只在测试链接使用 --wrap。正常消费者导入真实 emutls、析构登记、
LLVM Errno operators、非分配 errno 和原生出口；两种错误共享单元均拒绝。
8 轮 worker 重建验证初始 errno=0、模板=17、正常析构/再注册及槽复用，
最终 heap query=0，内核统计为每配置 37 个 FP 初始化和失效，空闲帧相等。

保留失败：compile02 的时钟绝对头路径拒绝，guest01 缺内核头锁，guest02
的未使用 helper Werror，guest03 把预期 OOM 的 HEAP FAIL 当意外失败，
legacy01 在链接终态前取得中间状态哈希。未禁用 Werror、未跳过输入校验；
新源视图/锁/观察器在全新目录重放。guest04 明确只允许一次完整 OOM 行并
核验 mode2 真实退出码，仍拒绝其他 FAIL。旧 kernel 源快照保留。

限制：最多四个 BSP 上下文；析构登记 OOM 返回 -1；FILE/同步/guard 仍有
单任务保护。本次没有完整 V8 ELF、JS、浏览器、视频、HTML5、SMP、NX、
stock 冷读回、公开工具重跑或远程 CI 资格。新 errno 不要求 malloc/emutls，
但线程局部存储本身仍受原有 64KiB 堆容量限制。
