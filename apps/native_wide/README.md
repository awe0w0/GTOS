# 原生宽字符与 UTF-8 转换

该模块由完整原生 V8 第 29 次最终链接的 `mbsrtowcs` 首错驱动，接入 libc++
宽字符时间输出及 codecvt 实际消费的 7 个 LLVM libc 接口与共享转换器。
它执行当前私有、单用户任务地址空间中的真实 IA32 对象。

保留固定版本的转换算法及 8 字节状态布局，显式修正过长 UTF-8、代理码点、超范围
码点、错误后的源指针及空源重启语义。SDK 状态类型与固定原始定义逐字节一致；
只为该类型接入 SDK，没有切换全局平台身份或放大容量。

实际完整目标第 30 次的 8 个提供者对象与客体执行对象逐字节一致。公开工具两次
冷启动共覆盖 12 个任务、14 次精确回收、48 组合法往返、80 组分段续接、224 组
带守卫的容量检查和 52 次非法输入拒绝；194 个错误日志控制均被观察器拒绝。
另检查源指针、零长度、页末、编码错误、读写/状态指针故障、取消及同伴存活。
此前夹具误把状态指针故障地址写成结构起点，失败产物保留；实际指令先读偏移 5，
修正夹具后重新冷启动通过，提供者与用户程序均未改变。

在已配置的构建主机上运行：

```sh
python3 tools/verify-native-wide.py /path/to/fresh-output \
  --integration-build /path/to/qualified-complete-native-build \
  --integer-builtins /path/to/libscalar_integer.a \
  --cxx /path/to/g++ --qemu /path/to/qemu-system-i386 \
  --firmware /path/to/qemu-firmware
```

工具要求完整目标真实链接尝试和冻结的源码锁，拒绝覆盖已有输出，核对提供者、
SDK、工具、输入及实际 ELF 页成本。GNU 编译器版本须为 13.3.0。
详细证据在 [acceptance.json](acceptance.json) 与原生集成状态文档。

本模块通过不代表完整 V8、Isolate、JavaScript、Content Shell、视频或 HTML5 已运行。
当前诊断不覆盖通用命名 locale、多用户线程或完整浏览器平台。
