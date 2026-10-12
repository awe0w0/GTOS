# 32 位 V8 完整原生目标集成入口

本模块把锁定的真实 V8、libplatform、Platform/Isolate/Context/Script 消费者和
GTOS 原生入口连接为 `native_embedder.elf` 目标。当前是显式启用的 IA32、单用户
线程、JIT-less 诊断配置，关闭 Wasm、ICU 和 sandbox。完整 ELF、客体 Isolate/JS、
浏览器、视频和 HTML5 尚未验收。当前阶段及首阻塞见[集成主线](../../docs/chromium-integration-plan.md)
和[机器清单](../../docs/chromium-integration-status.json)。

## 输入与复现

`overlay/`、`sdk/` 和 `c-sdk/` 保留平台覆盖、独立 C++ 配置及 LLVM C 声明闭包。
声明不等于运行服务。`payload-lock.json` 绑定包内每个文件，`source-lock.json`
绑定上游 revision、工具、Clang 资源头、Linux IA32 宿主库和实际使用的原头依赖。
源码版本仍为 Chromium 04bca2584cddb39e134fc7c16bb2886e02863d0e、
V8 be042d4462bee463c9b785701b8c6ee4576ee0a3；具体 DEPS、libc++、libc++abi、
LLVM libc、Abseil 和 zlib revision 以源码锁为准。原上游检出不被修改。

先准备锁定的源码依赖、sysroot、宿主 Python、Clang、GN、Ninja、IA32 宿主运行库
及整数 builtins，在 Linux/WSL 的 GTOS 仓库执行：

```sh
python3 -B tools/build-native-v8-integration.py /path/to/fresh-output \
  --v8-source /path/to/pinned-v8 \
  --clang /path/to/pinned-clang/bin/clang \
  --gn /path/to/pinned-gn --ninja /path/to/pinned-ninja \
  --host-i386-runtime /path/to/linux-i386-runtime/root \
  --integer-builtins /path/to/libscalar_integer.a \
  --enable-diagnostic --jobs 8 --build-timeout 7200
```

输出必须全新；失败、超时和旧缓存保留。`--prepare-only` 只生成 GN 图、完整依赖和
实际命令，默认执行完整 Ninja。输出的 `status.json`、编译数据库、链接命令和原始
日志是阶段证据；完整构建失败返回非零，驱动/准备错误不能记为 OS 根因。默认配置
不启用诊断目标，工具路径不能包含空格或 shell 元字符。

`--cache-from /path/to/retained/build` 将停止的缓存复制到新目录，再重新生成目标；
原缓存不变。相邻 `module-input-snapshot/source-lock.json` 必须存在并匹配固定版本
与工具。完成后将实际绑定的本模块原样冻结到 `module-input-snapshot/`，保存对应
构建脚本。缓存续编不是冷构建，也不是空白主机 bootstrap 验收。

## 已接入与验收边界

新增依赖由同一真实完整目标的错误驱动：实际 libc++ 流与 FILE 定义、真实诊断输出、
单用户任务 call_once、Ctype、分配式格式化、C++ ABI guard、C-only locale，以及
真实标准 FILE。旧命令、消费者和引擎参数保留，必要边界/回收验证后回放同一目标。

分配式格式化保留 double/80-bit long double 完整精度 f/e/g 路径。私有工作区避免
大整数临时副本，大型转换器使用真实堆和 RAII 回收；所测路径最大单函数帧 2236
字节，含 6000 位输出。当前强制单用户线程的私有地址空间选择官方 LLVM shared
errno 模式，避免 OOM 路径再次分配 TLS；不能据此声明多线程支持。

guard 使用固定 libc++abi 的官方 NoThreads 实现；初始化一次、嵌套、abort/retry、
递归终止、故障/取消和精确回收均已在客体验证。locale 只接入实际选中的
LLVM-libc/no_locale C-only 后端，不声明通用 POSIX 或命名 locale。第 26 次真实
链接已越过 newlocale/freelocale，三项实际对象与客体对象字节一致。

标准 FILE 直接复用已验收字节流算法。stdout/stderr 通过 GTOS 原生写入输出；
控制台读取和 fileno 明确报 ENOSYS，seek 报 ESPIPE。没有虚构的描述符表。
freopen 后返回实际正值原生文件句柄，可合法为 1/2；fclose 清理缓冲和句柄，静态
FILE 留在所属进程映像，公共与内部变量使用同一份指针存储。受影响的普通文件
回归及标准流资格见[标准流模块](../native_standard_streams/README.md)。

信号、异步采样、完整线程/等待、通用 POSIX、完整 libc/流功能仍未闭合。缺失
服务应明确失败，不能用空指针或成功占位隐藏。原生进程数 4、256 用户页、32
区域、64 KiB 堆、8176 字节用户栈和 16 KiB 陷阱栈没有放大；完整 ELF 产生后再
测量实际装载与内存需求。Linux 参考运行不能计为 GTOS 引擎或浏览器验收。
