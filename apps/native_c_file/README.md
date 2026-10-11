# IA32 原生 C 文件流组件

本模块把固定 LLVM libc 的 File 核心及 21 个字节流入口接到已验收的 GTOS 原生文件 ABI。文件实际存放在 ATA 上的 littlefs 卷；分配器、TLS errno、进程身份及原子锁均使用真实原生运行库。

此工作对应完整 V8 构建中 heap-profiler.cc/output-stream.h 和 sort-builtins.cc 的实际文件流依赖。完整集成仍须补齐并接入 libc++ 流定义和相关服务，见[集成主线](../../docs/chromium-integration-plan.md)。独立 FILE 资格通过只证明本模块语义。

接口为 fopen、fclose、fread、fwrite、fflush、fseek、ftell、fseeko、ftello、feof、ferror、clearerr、setvbuf、ungetc、fgetc、fputc、fgets、fputs、rewind、fileno 和 freopen。上游来源固定为 ebe33e01982dbbf879661e3b6b78450f3020a53f，每个来源文件的 Git blob 和 SHA256 见 upstream-lock.json。

## 适配与失败语义

平台回调每次最多传输 4096 字节，按实际返回值保留短读和部分错误。64 位 off_t 保持原样传给原生 seek；错误映射到私有 SDK 的 errno 数值。路径含终止 NUL 最多 512 字节。C 调用者仍须提供有效的指针、缓冲区和流对象；用户库不能替代内核对用户页权限的检查。

打开文件前先分配缓冲和流对象，分配失败不会创建或截断文件。fclose 即使缓冲写入失败，也会关闭底层句柄、移出流链表并释放对象。停止进程的未关闭句柄由安全内核 Reap 回收。fileno 返回原生句柄；它不是完整 POSIX 描述符表。freopen(nullptr,...) 缺少原生访问模式修改操作，明确返回 ENOSYS 并使旧句柄失效。

互斥锁在每次获取与释放时验证内核报告的进程身份和单用户线程能力，使用真实 acquire/release 原子操作。普通锁重复获取、定时/递归/robust/进程共享选项会拒绝执行；本组件不提供 flockfile，也不宣称多用户线程或 SMP 线程运行库已完成。

适配保留上游默认文件缓冲算法，并修复了关闭刷新失败的泄漏、无缓冲/行缓冲的错误码丢失、两种缓冲所有权下的重分配失败、clearerr 未清除 EOF、ungetc 成功时错误地清除已有错误标志，以及 ungetc/fputc 成功时未返回转换后字节的问题。具体差异见 upstream-adaptation.patch。实际客体失败日志与后续验收证据保存在任务独立产物目录，源码中没有用重试或忽略错误替代这些修复。

## 复现

使用固定 Chromium LLVM24、固定 libc++ 头文件、已验收的整数 builtins 档案、GCC13 内核编译器及 QEMU8。每次指定全新输出目录，保留先前构建与日志：

~~~sh
python3 tools/build-native-c-file-probe.py /tmp/gtos-c-file-new \
  --clang /path/to/chromium-llvm24/bin/clang \
  --libcxx-source /path/to/pinned/libcxx/src \
  --integer-builtins /path/to/libscalar_integer.a \
  --cxx /path/to/gcc13/bin/g++ --cc /path/to/gcc13/bin/gcc
~~~

source-lock.json 固定已执行来源、工具和产物；acceptance.json 汇总真实 guest 与最终自审证据。使用 --build-only 可在新目录复现对象、ELF 与装载内核哈希，但不会重新声称客体验收。完整构建后可用 tools/self-review-native-c-file.py 加一个全新输出目录和该构建目录重放自审。upstream-adaptation.patch 使用零上下文差异，应用时须指定 git apply --unidiff-zero；来源与每项适配见 upstream-lock.json。

O0 使用 32 MiB、一个固件 CPU；O2 使用 64 MiB、四个固件 CPU。QEMU 使用单线程 TCG 和 -icount shift=0,align=off,sleep=off，沿用原 ATA 有界轮询预算。该结果不代表周期精确模拟或 QEMU4 的主机时序已验收。

测试使用独立 2064 扇区介质，卷仅占中间 2048 扇区。测试覆盖 9001 字节分块写读、元素短读、EOF/错误标志、读缓冲下的当前位置、追加、排他创建、UTF-8、全流刷新、重新打开、PNG 的 1108 字节内容、堆耗尽、调用者缓冲与真实读/写/关闭错误。正常退出、保护页故障、外部取消、回收刷新失败和两种互斥拒绝路径均检查精确页面/句柄/FP 回收。独立未修改的上游 littlefs 只读挂载验证磁盘内容，观察器拒绝缺失、重复及篡改的验收记录。

进程数量 4、最多 256 页、32 个区域、64 KiB 堆、8176 字节可用用户栈及 16 KiB 陷阱栈均保持不变。内核仍须通过原有指令审计。

## 尚未闭合的能力

本次仅验收普通文件的 21 个字节流入口。标准输入/输出/错误流尚未接入，诊断配置显式保持空指针；完整格式化、扫描、宽字符流、POSIX、libc++ fstream 和完整 V8 Isolate 尚未完成。原始 long-double 格式化栈帧 8364 字节的问题仍保留，没有删掉默认语义或扩大用户栈。

生产内核的文件服务挂载、磁盘 PNG 解码到桌面窗口还需集成。现有可见镜像继续使用已验收的不可变资源 PNG 路径。本组件尚未接入完整 GN 浏览器链接；GTOS 原生 Chromium、网页浏览、视频及 HTML5 尚未运行。
