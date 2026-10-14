# V8 原生平台核心补齐

完整45的前三项缺口为 CreateTimezoneCache、SNPrintF 和 AdjustSchedulingParams。
本模块在原平台对象内实现这些接口，不改变 GN 图或扩展无消费者的运行库清单。

当前 GTOS 原生 PC 配置明确采用不可变的 UTC 本地时区、无夏令时。缓存对
有限日期返回 UTC，非有限日期返回空名称；两种 Clear 模式保持此政策。
缓存通过真实 new/delete 分配，支持共享读取，无宿主 TZ 读取。GTOS 已有
独立的 CMOS/PIT UTC 墙钟服务，本模块没有用 uptime 代替 epoch，也没有
声称支持可配置时区或完整 DateCache/Isolate。

SNPrintF/VSNPrintF 调用真实 LLVM vsnprintf，遵循固定 V8 的失败/截断返回-1
约定；负长度不转为巨大 size_t，正容量失败保证末字节 NUL。固定 BSP 和
默认优先级无需调整调度参数，该初始化钩子仍检查真实原生配置。

32 MiB/1 vCPU、64 MiB/4 vCPU 的共享及默认单线程配置共 76 个上下文、
91854 项检查通过。覆盖共享缓存、重复创建销毁、NaN/无穷/有限极值、UTC
偏移与 Clear、可变参数、浮点、64位整数、精确容量、截断和哨兵。继承的
线程/每线程存储、取消、OOM、页故障、FP 和页帧回收继续通过。

完整46按真实顺序重建38成员归档，仅替换平台成员；实际链接输入及新对象
完整编译依赖核对通过，旧缓存未变。前三项缺失符号消除，首错变为 isspace。
诊断仍非穷尽，完整 ELF、V8 Isolate、Chromium 网页、视频和 HTML5 未验收。

FOpen 新 stripped ELF 与既有验收版本相同，沿用旧8任务/8回收证据，本轮
未重复运行 FOpen 客机。公开源码和探针与私有验收字节核对，未声称公开
工具或远程 CI 重新执行。生产镜像未改动，整个 V8 调用链栈界仍未证明。
