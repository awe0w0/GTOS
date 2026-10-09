# GTOS 原生 C 字节与字符串组件

此模块保存固定 LLVM libc 的 13 个默认上游提供者：memcpy、memmove、memset、memcmp、memchr、strlen、strcmp、strncmp、strchr、strstr、strcpy、wmemchr、wcslen。实际选定引擎对象对这组符号共有 1011 个“对象/符号”引用；这不是整引擎已链接的证明。

全部实现和 151 个源码/头文件/许可依赖来自固定版本 ebe33e01982dbbf879661e3b6b78450f3020a53f，保持默认算法、打包方式、优化和 ISA 配置。导出 O2 对象与实际 GN 对象逐字节一致。13 个提供者组成的整库无未定义符号，实际 GN 代码为3473字节。IA32 的 size_t、wchar_t 均为4字节。

实现直接访问调用者的 C 对象，保留通常的长度、可读写范围、终止符及重叠前置条件。memmove 支持重叠；memcpy 的测试使用不重叠对象。非法映射由真实用户 #PF 隔离，不转换成 EFAULT。搜索字符按 unsigned char 截断，比较结果只保证负、零、正的关系。字符串终止符以外的字节不属于可访问输入。

## 构建与复现

使用已锁定 C 时钟模块的 SDK、LLVM TLS errno、emutls、VM heap，以及固定 libc++、Clang24、GCC13.3、IA32 整数档案，并配置 GRUB/QEMU：

~~~sh
python3 -B tools/build-native-c-string-probe.py /path/to/fresh-output \
  --clang /path/to/pinned-clang/bin/clang \
  --libcxx-source /path/to/pinned-libcxx/src \
  --memory-cxx /path/to/qualified-gcc13/bin/g++ \
  --integer-builtins /path/to/qualified/libscalar_integer.a
~~~

输出目录必须全新。默认独立编译 O0/O2，校验上游及伴随输入、13 个真实 GN 对象、全部服务对象、八个 ELF、两个内核的完整装载内容和两个纯字符串档案，再执行四次冷启动。--build-only 只重建、比对已有资格，不产生新的来宾通过结果。

纯字符串档案不包含旧 GNU 标量 memcpy/memset；在同一个原生用户程序中选择这组提供者时，应移除旧重复提供者。内核测试自己的整数字节助手保持原有实现。此模块不扩展旧运行库的整库资格，也不提供头文件中其他尚未实现的 C 函数。

## 已执行验收

独立模块 O0/O2 × 32MiB/1配置CPU、64MiB/4配置CPU，共四次冷启动、20个真实 CPL3 任务、24次精确 Reap。另用实际 GN 对象执行两次冷启动、10个任务、12次精确 Reap。配置四 CPU 不表示多核原生执行。

每个任务完成99,063组用例和10,639,636个基础检查。覆盖16×16对齐组合、长度及向量边界、13种重叠方向、全部256×256字节比较、字符截断、首个命中、高位字节、重复子串、4字节宽字符、结束于guard页前的字符串与宽字符串，以及0到4096字节页尾操作。普通指针前后哨兵逐字验证；零长度位于无权限边界时，当前实现未访问该页。

模式0正常退出；模式1由真实 memcmp 产生 #PF14/error4/CR2=80004000；模式3由真实 memset 产生 #PF14/error6/相同CR2。模式2至少完成100次真实额外比较及yield后被外部取消73。全部13项显式调用计数和完整记录由独立判定器精确核对，取消模式按实际重复数增加计数。

预留5页，中间3页可读写，两侧guard页不分配帧。私有LLVM errno来自64KiB堆，整个动态成本为3个测试页、16个堆页及1个页表，另加ELF、用户栈、页目录和FP页。停止后仍可读完整测试页和errno；Reap后页帧、区域及FP所有权恢复原基线，其他任务持续推进。

130个错误日志反例、50个损坏跳转表反例和5类禁止ISA反例均被拒绝。12个ELF的全部实际执行输入通过legacy x87/SSE/SSE2审计。保守调用栈最大O0为1460字节、O2为844字节，低于8176；默认五类只读跳转表保留，按无符号索引界、CFG支配关系及同函数指令目标逐项证明其不增加栈帧。证明器保存在probe/jump_tables.py，证据摘要保存在acceptance.json。

完整原生V8 Isolate、Chromium网页浏览、视频和HTML5尚未运行。默认printf的long double转换仍超过当前用户栈；文件系统和线程服务仍待闭合。生产镜像及可见虚拟机保持已验收PNG桌面，内核容量未改变。
