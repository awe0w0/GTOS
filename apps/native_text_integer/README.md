# 原生字符分类与整数解析

完整46的首错为 isspace，同时缺失 isprint、strtol 和 strtoll。本模块在原有
native_ctype_libc / native_numeric_libc 中接入固定 LLVM 的四个源文件。函数体
保持原样，不改变引擎配置或使用替代桩。LLVM 修订及源码、对象 SHA256 见
[验收记录](acceptance.json)。

isspace/isprint 采用固定 C locale，覆盖 EOF、全部256个字节及范围外输入。
IA32 上 long 为32位，long long 为64位；解析覆盖正负边界、溢出饱和、ERANGE、
无数字、空白、符号、进制0/2/8/10/16/36、十六/二进制前缀及空 endptr。
固定 LLVM 在非法进制 EINVAL 时保留 endptr，成功和无数字路径保留原 errno。
探针通过 volatile 函数指针调用公开 C 符号，避免常量折叠代替实际调用。

整数对象沿用 errno 模式3声明，链接已验收的真实 external 模式4 Errno 定义；
赋值操作最终访问原生每线程 errno。64位除法使用既有真实整数运行库。
两种 errno 声明的 ABI 已核对，不为改变未定义符号断言而改写上游源码。

新 GN 图含5183条对象命令，原5179条命令及最终输入顺序保持，只新增四对象。
四对象采用确切 GN 参数，编译前 -M 与编译后 -MD 完整依赖均锁定核对。
32 MiB/1 vCPU、64 MiB/4 vCPU 及默认单线程配置共76上下文、145358项检查、
48次 Join 通过；正常结束、取消、TLS OOM、页故障均精确回收任务、FP、堆及页帧。
错误共享 errno 和 TLS 的负对照被拒绝。继承的线程、时区、格式化测试保持通过；
探针内核保留旧 THREAD TLS 日志标签，验收以实际新调用者和输入锁为准。

完整47保留原平台归档，按真实新 GN 顺序加入四对象；实际链接文件、配置和
新对象完整来源依赖核对通过，旧缓存未变。四项缺失符号消除，下一处首错为
OS::SetDataReadOnly。诊断仍非穷尽，完整 ELF、V8 Isolate、Chromium 网页、视频
和 HTML5 尚未验收。公开文件与私有验收字节一致，未声称公开工具或远程 CI
重新执行。生产镜像未改动，整个 V8 调用链栈界仍未证明。
