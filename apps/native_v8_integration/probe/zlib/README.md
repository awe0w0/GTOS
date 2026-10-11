# 完整 V8 目标中的 zlib 原生文件消费者验证

本夹具消费固定 `456ae730017b9b4bd3371927abc82627fd51feef` 的真实 zlib、完整目标的
原生入口、LLVM strerror 和六个格式化模块。验证范围是 gzip 解码、GTOS 文件后端、
错误传播、容量失败、退出及资源回收；完整 V8、JavaScript、浏览器和压缩成功仍未验收。
完整目标已越过这些 zlib 对象，当前阻塞为 cctz 时间/时区及文件接口。

## 实现与契约

GTOS 选择上游完整标量编解码分支，关闭可选 SIMD 加速，保留完整 gzip 对象和许可。
`gtos-file.h` 直接调用已有原生 File ABI；正数句柄属于当前任务，不建立伪 Unix fd 表。
路径限制 512 字节，每次 I/O 最多 4096 字节，先检查整个 32 位地址区间是否溢出。
实际用户页权限和句柄所有权由内核检查。seek 请求保留有符号 64 位输入，底层文件位置
仍限制在 INT32 范围；不支持的操作明确失败，继承/close-on-exec 模式未实现。

固定上游在重复读取截断 gzip 时可能清除 Z_BUF_ERROR，随后 EOF 分支没有恢复它。
GTOS 的两处 EOF 分支仅在仍处于 GZIP 解码状态时保留该错误。完整流、透明文件与
拼接流保留原行为；补齐尾部并调用 gzclearerr 后可以继续解码。Linux 原分支不变。

实际追加负例暴露已打开 FileStore 读句柄仍保留旧文件长度。FileStore 仅在确实修改过
的写句柄 Sync/Close 成功后更新同一文件的只读缓存，保留读游标及所有权。失败提交、
未修改的旧写句柄关闭、其他文件及删除后替换的路径均不刷新。该修改不承诺多个写
句柄之间的并发一致性；服务仍为 BSP 串行模型。类定义、句柄容量及 LittleFS 上游算法
保持原样，关联存储、File ABI 和 C FILE 回归已在相同 FileStore 对象上执行。

## 复现

准备完整目标的保留输出及 `module-input-snapshot`、已执行的 C FILE 输出、锁定的
Clang24/GCC13、整数 helper archive 和 QEMU8/GRUB 工具。必须使用新输出目录：

```sh
python3 -B tools/build-native-v8-zlib-probe.py /path/to/fresh-output \
  --complete-build /path/to/retained-complete-v8-build \
  --qualified-file-build /path/to/executed-c-file-build \
  --clang /path/to/pinned-clang/bin/clang \
  --cxx /path/to/qualified-gcc13/bin/g++ \
  --integer-builtins /path/to/qualified/libscalar_integer.a
```

入口先核对冻结集成 payload、上游来源、编译器、Clang 资源头、41 个 C FILE 运行库
对象及已执行的内核对象，再从真实 GN 编译命令构建消费者。O2 的 zlib 对象必须与
完整目标逐字一致，内核继续执行原整数指令审计。`source-lock.json` 另行锁定本夹具和
工具源码，以及每组 25 个实际消费者对象。`--component-cache` 只复用同编译器、同
编译命令和同依赖内容的对象；对象必须同时符合已执行清单及本模块资格锁。

## 客体检查与限制

O0/32 MiB/单 vCPU 和 O2/64 MiB/四 vCPU 各执行六种任务模式：正常解码、错误/边界、
未映射页、只读页、立即退出和活跃同伴取消。验证 12 KiB 解码校验值、短读写、seek、
截断/CRC 损坏、追加继续解码、ENOMEM/EIO、所有权及文件句柄回收。正常返回执行
自动/TLS/fini 析构，异常路径跳过；最终 16 个句柄全部回收、开放句柄为零，帧精确
恢复，同伴、ring0 与启动任务继续推进。每组另有 204 个观察器负例。配置四 vCPU
不表示原生程序已支持多核执行。

实际 deflateInit2 小配置请求共 74164 字节，其中 65536 字节分配在当前 64 KiB 堆下
失败，返回 Z_MEM_ERROR。已成功分配的四块全部释放，前后最大可分配载荷均为
53088 字节，并实际重新分配该块验证回收；不把这项失败回滚验收称为压缩成功。
用户页/ELF 页 256、动态区域 32、进程槽 4、堆 64 KiB、可用用户栈 8176 字节和
陷阱栈 16 KiB 均未扩容。8364 字节 long-double 格式化帧仍未解决，字符串错误信息
路径通过不能验收 long-double 格式化。所有测试使用独立磁盘，不修改生产镜像或
可见虚拟机磁盘。最远真实阶段与下一阻塞见[集成主线](../../../../docs/chromium-integration-plan.md)。
