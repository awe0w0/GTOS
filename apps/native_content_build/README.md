# 前沿 Content 生成入口（私有候选）

本目录保存的内容是：**对 GTOS 移植树根 `BUILD.gn` 的最小改动、把这次生成跑法固定下来的重放脚本，以及实际记录。** 它不是浏览器运行能力，也不代表 GTOS 平台已经能跑 Chromium。

## 1. 改动是什么

共享移植树 `sources/chromium-gtos/src/BUILD.gn` 目前对任何 GTOS 浏览器目标直接拒绝：

```gn
if (is_gtos) {
  assert(false, "GTOS browser runtime is not implemented: libc/C++ runtime, VM, threads, IPC, Ozone and networking are required. …")
}
```

这个拒绝是对的：运行浏览器所需的 libc/C++ 运行时、VM、线程、IPC、Ozone 与网络都还不存在。候选补丁把它改成**默认仍拒绝、只有显式点名才放行一条路**：

```gn
declare_args() {
  gtos_frontier_bringup = false     # 只有显式 bring-up 调用会打开
}
if (is_gtos && !gtos_frontier_bringup) {
  assert(false, "<原文逐字不变>")   # 未设 / false 时与原拒绝完全一致
}
```

外加一行 `GTOS-FRONTIER current_os=… target_os=… bringup=…` 打印，让每次生成自己说明走了哪条路。`bringup-patch.py` 是这段改动的唯一实现，锚点不唯一时直接失败，不做模糊匹配。

`.gn` 的另一处最小改动是把 `script_executable` 从仓库自带的 hermetic 解释器 `//third_party/cpython3/host/bin/python3`（本树里不存在，`exec_script` 一律返回 127）改成 host 解释器 `/usr/bin/python3.8`。这是**宿主侧**的绑定，与目标语义无关；其余 dotfile 内容逐字保留。

改动只写进私有候选视图，共享移植树与 canonical 树**只读**：`replay.sh` 和 `bringup-patch.py` 都对目标做 realpath 检查，落在共享树内即拒绝。视图里的 `BUILD.gn`/`.gn` 采用"先 unlink 再写新文件"，避免通过硬链接写回源树。

## 2. 已记录的真实结果

生成命令与首错（2026-10-30，`c1-content-154-01..11` 各阶段留证）：

| 调用 | 参数 | 结果 |
|------|------|------|
| 普通生成 | `target_os="gtos" target_cpu="x86"` | **原拒绝**：`ERROR at //BUILD.gn:29:3: Assertion failed`，消息逐字为 "GTOS browser runtime is not implemented…" |
| 真实目标 + bring-up | 同上 + `gtos_frontier_bringup=true`，`--root-target=//content/shell:content_shell_app` | 越过根拒绝，**首个真实错误**：`//build/config/rust.gni:348:3: Assertion failed: assert(rust_abi_target != "")` |
| 同上 + `enable_rust=false` | 追加 `enable_rust=false` | 错误前移到 `//build/rust/gni_impl/rust_target.gni:43:3: Assertion failed` |

也就是：**这条目标链当前卡在 rust 目标三元组缺口上，还没走到任何 C++ 编译**。`content/shell` 的依赖闭包无条件引入 `build/rust/rust_static_library.gni`，而本移植树没有 gtos 的 rust target，因此 C2（`shell_main_delegate.cc` 的 CXX 首错）与 C3（链接首错）在本轮**未被触及**，不能声称已取得。

`GTOS-FRONTIER current_os=gtos target_os=gtos bringup=false` 出现在普通生成里；带 `--root-target` 的生成不会加载根 `BUILD.gn`（GN 只加载目标闭包），所以 bring-up 那次没有这行打印——这也说明根拒绝只在根文件被加载时生效，带 `--root-target` 的生成**从来不受它约束**，这是移植树原本就有的空洞，候选补丁没有改变它，也没有假装补上。

## 3. 重放

```sh
GTOS_VIEW=/var/tmp/…/view/root \
GTOS_GN=/var/tmp/…/bin-v8/gn \
GTOS_PORT=/mnt/f/GTOS-Chromium/sources/chromium-gtos/src \
GTOS_OUT=/var/tmp/…/out/frontier-replay \
sh apps/native_content_build/replay.sh
```

`replay.sh` 只在"普通生成仍复现原拒绝"时退出 0；真实目标的成功或首个错误都照实打印，并在错误文本不含 `GTOS_EXPECT`（默认 `rust`）时失败，以便移植树变化被发现。

## 4. 边界

- 不写共享移植树、不写 canonical 树、不写共享端口/canonical 源；补丁只进私有候选视图。
- 未生成任何目标、未编译任何 Chromium 源、未链接、未运行；本轮只有 GN 结论。
- 不宣称浏览器可运行：开关只解除"根文件一律拒绝"这一条，其余缺口（rust、libc/C++、VM、线程、IPC、Ozone、网络）一个都没补。
