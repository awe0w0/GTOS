# 原生 runtime_error 终止

完整原生目标第 30 次最终链接的真实首错是 libc++ 的 `__throw_runtime_error`。
本模块保留固定 `stdexcept.cpp` 的函数体和无异常分支，通过已验收的原生回调输出
诊断并终止进程，不启用异常、不返回成功，也不引入通用异常运行库。

实际 GN 对象与客体执行对象逐字节一致。私有与公开工具均通过 32M/单 CPU、
64M/四 CPU 两次冷启动：长消息、空消息、实际消费者的 `locale not supported`、
非法消息指针故障以及恢复任务，共 12 个任务、14 次精确回收。独立自审拒绝
200 个错误证据样例，公开用户 ELF 与私有验收 ELF 字节一致。

第 31 次直接重放当前 GN 的真实最终链接命令，保留 5147 条旧对象命令、原顺序
输入和引擎参数，旧 Ninja 缓存只读，仅增加这个必要对象；没有再次执行完整 Ninja
编译，也没有复制整套缓存。该首错已越过，下一实测首错是 `sscanf`。

构建及客体验证必须使用临时无网络空间、最小环境和固定工具指纹，隔离不可用时
停止执行。详见 [执行栅栏](../../docs/native-command-execution.md)。参数化工具使用
已经准备的 GN 构建、实际最终链接记录和已冻结的工具验收记录：

```sh
/usr/bin/env -i PATH=/usr/bin:/bin LANG=C.UTF-8 TZ=UTC \
  /usr/bin/unshare --user --net /usr/bin/python3 -I -B \
  tools/verify-native-runtime-error.py /workspace/fresh-output \
  --workspace /workspace --runtime-root /workspace/toolchains/gtos-runtime \
  --integration-build /workspace/prepared-native-target \
  --link-evidence /workspace/actual-final-link/status.json \
  --tool-state /workspace/qualified-tools/status.json \
  --integer-builtins /workspace/libscalar_integer.a \
  --cxx /workspace/g++ --qemu /workspace/qemu-system-i386 \
  --firmware /workspace/qemu-firmware
```

工具要求配套冻结的源码锁和指纹，不接受未审查的脚本、下载或日志命令。
证据位置及 SHA256 见 [acceptance.json](acceptance.json)。SDK 冻结复用已验收基线
加四文件差量，旧源码、失败、缓存、生产虚拟机镜像和磁盘均保留。
该局部模块通过不代表完整 V8、Isolate、JS、浏览器、视频或 HTML5 已运行。
