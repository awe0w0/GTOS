# 完整 V8 熵读取阻塞的 GTOS 客体验证

完整 `native_embedder` 在 `seed_material.cc:17` 进入 POSIX 文件分支，缺少 fcntl.h。
GTOS 覆盖改为调用原生 CPL3 硬件源：检测 CPUID 的 RDRAND 位，再按 CF 判断是否
取得有效的 32 位数；每个数最多尝试十次。没有时钟、固定种子或伪造成功的回退。
完整续编 portable-resume-06 已越过该对象，当前首个失败为 futex.h:31 的同步依赖。
完整 ELF、V8 Isolate、JavaScript 与原生浏览器尚待验收。

这里执行真正的 Abseil `ReadSeedMaterialFromOSEntropy`，保留上游其他函数和
Linux 分支。GTOS 分支检查块数乘法溢出，原生源检查空指针、地址加法溢出和
当前用户地址范围，逐字节提交最后不足四字节的部分。未映射或只读用户页由实际
CPL3 页权限拒绝，产生可恢复的用户故障。失败时不得使用已写前缀；此接口不承诺
事务性写入，也不新增内核随机 syscall。

原生源要求当前 CPU 暴露 RDRAND。不支持或重试耗尽时返回 false；空字节请求为
无操作。V8 入口的 `--random-seed=42` 是确定性诊断参数，不能当作系统熵来源。
实现参考 [Intel DRNG 指南](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-digital-random-number-generator-drng-software-implementation-guide.html)。
QEMU 8.2.2 的 [RDRAND 实现](https://github.com/qemu/qemu/blob/v8.2.2/target/i386/tcg/int_helper.c)
调用其[随机后端](https://github.com/qemu/qemu/blob/v8.2.2/util/guest-random.c)；测试命令
不设置 `-seed` 或录制/重放随机流。样本变化检查用于发现固定回退错误，不能证明
真机熵质量或 TLS 安全。重试耗尽分支尚无实际硬件故障注入证明。

## 客体矩阵

每组运行六个独立 ELF：跨页的实际种子读取、长度/地址边界和字节尾部、未映射页
写入、只读页写入、立即退出，以及持续读取的用户同伴。每个程序持有两个真实
FILE 和 64 KiB VM 堆。正常返回执行自动、TLS 和 fini 析构并刷新缓冲；故障、
立即退出和取消跳过用户清理，资源在内核 Reap 精确回收。

O0/32 MiB/单 vCPU 与 O2/64 MiB/四 vCPU 分别开启及关闭 RDRAND。关闭源后，
非空读取返回 false，目标缓冲保持原值，原本会写故障页的调用也明确失败并正常
返回。同伴、ring0 和启动任务继续推进。用户调度仍限 BSP；容量及整数 ISA 审计
保持原有约束。完整 V8 初始化、GetSaltMaterial、熵池及通用线程不由此夹具验收。

## 重放

需要准备好的完整目标及冻结 `module-input-snapshot`，以及已验收的 C FILE 输出；
输出必须是新目录。按[完整目标模块](../native_v8_integration/README.md)准备输入。

```bash
export PATH=/path/to/qualified-qemu8/bin:$PATH
export GTOS_QEMU_DATA_DIR=/path/to/qualified-qemu8/firmware
python3 tools/build-native-v8-seed-probe.py /new/output \
  --complete-build /path/to/completed-native-embedder-build \
  --qualified-file-build /path/to/qualified-native-c-file-build \
  --clang /path/to/pinned/clang \
  --cxx /path/to/qualified-gcc13/g++ \
  --integer-builtins /path/to/libscalar_integer.a
```

工具逐组新建并显式格式化独立的 2064 扇区测试盘，核对两端保护区；不接受生产
应用盘路径。保留源码快照、实际 ELF、编译/链接及整数审计日志和客体证据。
普通 Python 生成的缓存目录保留，源绑定只读取文件。
