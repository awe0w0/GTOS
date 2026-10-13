# 原生基础数学提供者

完整V8链接中的ceilf、ceil、floor、fmod和abs缺口驱动本模块。新增
native_numeric_libc使用固定LLVM原实现、既有整数builtins和shared errno。
目标局部启用-fmath-errno，保留引擎参数和已有fabs/sqrt目标。

32M/1CPU与64M/4CPU共12个CPL3任务、12次精确回收通过。每任务921项数学
检查，四舍入模式覆盖符号零、次正规数、整数邻值、最大有限值、无穷、NaN、
余数域错误与实际MXCSR invalid；检查x87/MXCSR控制位跨调度保持。
abs避开标准未定义的INT_MIN。原FILE/PNG、标准流、格式化、OOM、故障、取消
及资源回收继续验证：共172918项检查，444个日志反例被拒绝。
独立stock LittleFS对两盘实际冷读回，磁盘字节保持。

probe/冻结实际消费者、客体内核、观察器和独立读者源码。固定版本、工具、
完整依赖、编译/链接命令及客体日志由acceptance.json引用的工作区产物绑定。
公开源码逐字节核对，未重跑公开GN/客体或新远程CI。

资格限于私有IA32单用户任务配置，不穷尽浮点位模式，不证明并行线程或全调用
链栈界限。完整38已实际链接，12380项输入及旧缓存终验通过；本次首错为
libc++ locale.cpp所需的__cxa_atexit。默认20项诊断不代表全部缺口，五项数学
符号不再出现在本次诊断。完整V8 ELF、Isolate/JS、浏览器、视频和HTML5仍待
验收。生产镜像本批未更新；详情以acceptance.json为准。
