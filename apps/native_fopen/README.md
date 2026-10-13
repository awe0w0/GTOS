# 原生 V8 FOpen

完整43的真实首错是V8 OS::FOpen。本模块新增最小GTOS桥接，使用已验收
的真实fopen/FILE通路。固定POSIX语义要求普通文件；GTOS NativeFiles /
FileStore的file-open通过固定LittleFS拒绝非regular类型，目录走独立ABI，
因此此诊断profile的真实fopen已执行同等约束，不需要伪造fstat。

GN保留5175旧对象命令、引擎参数与最终次序，只增加gtos-file一个对象。
消费者通过volatile函数指针调用真实V8符号，禁用builtin；缺桥接负链
必须失败在V8 FOpen，正向ELF没有未定义符号。没有直接fopen替代测试。

每单位72项检查覆盖真实write/read/update/append、独立字节预期、排他
创建/EEXIST，以及r/w/a目录拒绝、缺失/空/长路径/非法mode拒绝。真实
malloc压力导致打开失败，证明OOM不能创建或截断文件；释放后原内容
保持并恢复打开。早期remove测试清理依赖的链接失败保留，改用固定
任务/循环编号文件名，不删除或放宽任何FOpen检查。

两配置32M/smp1、64M/smp4各四任务：正常关闭返回、带活跃FILE/堆的
外部取消、实际用户故障，以及四次完整重复/OOM恢复。总8任务/8精确
回收、1036项消费者检查和314项日志反例拒绝。内核核对实际CPL3/私有
CR3、ATA读写/flush、FILE owner、保留堆和帧成本，再恢复句柄/堆/帧/FP
所有者，同伴继续。没有人工磁盘错误注入或新stock独立冷读回。

五个ELF加载页，提供者单函数0字节，消费者最大单函数60字节，
trap/ATA深路径观测2332字节；不证明完整调用链栈界或并行用户线程。
编译锁定实际GN/profile和完整pre-M/post-MD闭包；小对象阶段不重哈希
无关V8源，正式GN及完整目标另行检查全输入。公开字节对应不等于
公开GN/客体、远程CI或stock重跑。完整V8 ELF/Isolate/JS、网页、视频、
HTML5仍待验收，生产镜像未改。
