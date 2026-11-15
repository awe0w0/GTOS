# 桌面条形屏 A3 上的 GTOS

A3 目标使用 ESP32-P4NRW16X v3.x 的 RV32 高性能核、16 MiB Hex SPI PSRAM 和
16 MiB SPI NOR。ESP-IDF 提供芯片启动、FreeRTOS 任务和 SoC 驱动，GTOS 通过
CPU HAL 运行自己的堆分配器、启动日志和原有的 GTAPP 解释器。C6 另行编译为
4 MiB 的 ESP-Hosted 协处理器固件。

IA32 内核的单 CPU 临界区也使用这层 HAL。原有 GTAPP 包可在两个目标运行，
PC 原生 SDK 和 Chromium 使用现有 IA32 进程 ABI。A3 外设服务通过
[GTOS Board API](../../../include/drivers/board.h) 提供给嵌入式应用。

## 构建与虚拟机

在 Linux 或 WSL 激活 [ESP-IDF v6.1](https://github.com/espressif/esp-idf/tree/v6.1)
的 ESP32-P4、ESP32-C6 工具后，从 GTOS 仓库根目录执行。首次构建按项目的锁文件获取
ESP-Hosted 和 Wi-Fi Remote。

~~~sh
make a3-riscv a3-riscv-c6
python3 arch/riscv32/esp32p4/tests/verify_core.py \
    --emulator /path/to/esp-emu --build out/a3-riscv --output out/a3-core-test
python3 arch/riscv32/esp32p4/tests/verify_board.py \
    --emulator /path/to/esp-emu --build out/a3-riscv \
    --parts /path/to/esp-emu-1.0.0-examples --output out/a3-board-test
PYTHONPATH=/path/to/esp-emulator-v1.0.0/tools \
    python3 arch/riscv32/esp32p4/tests/verify_live_board.py \
    --emulator /path/to/esp-emu --build out/a3-riscv \
    --parts /path/to/esp-emu-1.0.0-examples --output out/a3-live-test
python3 arch/riscv32/esp32p4/tests/verify_radio.py \
    --emulator /path/to/esp-emu --build out/a3-riscv --c6 out/a3-riscv-c6 \
    --parts /path/to/esp-emu-1.0.0-examples --output out/a3-radio-test
~~~

两个默认输出目录各有 ELF 和合并 Flash 镜像。测试使用
[ESP-EMU 1.0.0](https://github.com/espressif/esp-emulator/releases/tag/v1.0.0)
及其公开示例包中的按键、编码器模型。每次验证传入新的输出目录，保留 UART 日志和
1424 × 280 PNG。使用以下命令可以单独检查 P4 启动。

~~~sh
esp-emu --chip esp32p4 --psram-size 16M \
    --firmware out/a3-riscv/gtos-a3-flash.bin --elf out/a3-riscv/gtos-a3.elf \
    --timeout 20s --exit-on 'GTOS_A3_CORE PASS psram=16777216 checks=0'
~~~

Core 检查嵌套中断状态、GTOS 启动日志、损坏的 GTAPP 拒绝路径、现有 Catch 输入和
PSRAM 回收。它做两次冷启动，并检查 8 MiB PSRAM 的容量拒绝。

实时外设用例从同版 [ESP-EMU 源码](https://github.com/espressif/esp-emulator/tree/v1.0.0/tools)
加载 `cosim_tcp.py`，用外部 I²C 模型替换固定寄存器数据。它验证 IMU 改变后星盘重绘，
观察 VCI、复位顺序和 GPIO26 上的 1 kHz / 60% 及 65% PWM。充电用例检查限流、
充电电流和稳压设定先于 GPIO28 拉高，并验证拔线、充电故障、Power Good 丢失、
CC 总线无应答及退出时切断充电。重新插线后需要显式启用。
`signals.json` 保存事务、引脚边沿和故障注入时的虚拟机时间。

手动启动双机时指定一个新目录。脚本以副本作为 P4 的 Flash，并将 C6 串口记录在该目录。

~~~sh
bash arch/riscv32/esp32p4/sim/run.sh /path/to/esp-emu /path/to/esp-emu-1.0.0-examples \
    out/a3-riscv out/a3-riscv-c6 out/a3-vm
nc 127.0.0.1 5567
~~~

UART 使用 5567，另开终端连接 5566 可操作虚拟器件。发送
*part next input press* 和 *part next input release after 180* 模拟按键，
*part encoder input cw* 转动旋钮。端口可以用 A3_UART_PORT 和 A3_CONTROL_PORT 覆盖。
打开 RADIO 页并等待取得地址后，状态接口可从宿主机读取。

~~~sh
curl -fsS http://127.0.0.1:18080/status
~~~

`A3_HTTP_PORT` 可以覆盖宿主机端口。脚本仅在回环地址转发到虚拟 C6，使用
[ESP-EMU 的 hostfwd](https://github.com/espressif/esp-emulator/blob/v1.0.0/docs/guides/NETWORKING.md#host--guest-via-hostfwd-qemu-compatible-syntax)。
对 UART 发送 `q` 会结束两颗虚拟芯片的运行。

## Desktop Orbit

桌面星盘通过 IMU 的加速度和陀螺仪数据移动光点。NEXT、BACK 切换星盘、电源、无线，
WAKE 控制屏幕休眠，旋钮调亮度，按下旋钮用 NVS 保存当前页面和亮度。电源页显示
TUSB320LAI 检测到的方向和电流档位，以及 BQ25601 限流和充电状态。无线页向 C6
供电，经四位 SDIO 查询固件版本、发起 Wi-Fi 扫描并发出 GTOS Orbit BLE 广播。
虚拟板随后加入 `A3-LAB` 的 WPA2 网络。P4 取得 DHCP 地址后，`GET /status`
返回姿态、编码器、亮度、USB-C 和充电状态。无线测试从宿主机经 C6 Wi-Fi 和 SDIO
读取数据，操作旋钮、切换充电后再检查响应，并验证接入点缺失时的失败与重试。
在无线页通过 UART 发送 `w` 可重试加入网络。

`GTOS Orbit` 也接受一台 BLE 客户端，断开后恢复广播。只读 GATT 服务使用
`01d084ac-5c80-448a-aca7-8fd0072e67d4`，状态特征使用
`9c7c1669-67bc-493f-a1de-6ecf993ad223`。状态每次读取为 19 字节。

| 偏移 | 类型 | 值 |
| --- | --- | --- |
| 0–3 | 4 × uint8 | 格式版本 1、页码、亮度百分比、状态位 |
| 4–9 | 3 × int16 | XYZ 加速度，mg |
| 10–11 | int16 | Z 角速度，度/秒，向零取整 |
| 12–13 | uint16 | USB-C 声明的电流，mA |
| 14 | uint8 | BQ25601 故障位 |
| 15–18 | int32 | 启动后的旋钮步数，顺时针增加 |

多字节值使用小端序。状态位 0、1、2、3 分别为屏幕唤醒、IMU 数据有效、USB-C 已连接、
充电使能。BLE 用例用独立客户端验证发现、连接、读数、拒绝写入和断开后重播。运行时
使用 Python 3.12，并在测试虚拟环境安装 [Bumble 0.0.236](https://pypi.org/project/bumble/0.0.236/)。

~~~sh
python3 arch/riscv32/esp32p4/tests/verify_ble.py \
    --emulator /path/to/esp-emu --build out/a3-riscv --c6 out/a3-riscv-c6 \
    --parts /path/to/esp-emu-1.0.0-examples --output out/a3-ble-test
~~~

虚拟板在 [sim/a3.json](sim/a3.json) 接入按键、旋钮和三个 I²C 寄存器模型。板级驱动
执行真实的 ESP32-P4 I²C、GPIO、PCNT、LEDC 和 Flash 操作。面板显示由 RGB565
帧缓冲模型接收，USB 和电池只验证软件可读写的信号与寄存器。I²C 模型的加速度、
充电状态和 CC 档位可在该文件更改。UART0 和 USB Serial/JTAG 分别接入应用命令，
发送 s 读状态，f 取虚拟帧，c 切换虚拟充电，q 结束虚拟应用。

## A3 实板配置

| 连线 | A3 配置 |
| --- | --- |
| I²C | P4 SDA 7、SCL 8，外部上拉。LSM6DS3TR-C 0x6A、BQ25601 0x6B、TUSB320LAI 0x47，均为 7 位地址 |
| 操作 | WAKE 2、NEXT 11、BACK 12，旋钮 A/B/SW 20/21/22，按下为低电平 |
| 控制 | P4 的 C6 电源 5、C6 复位 6、VCI 10、LCD 复位 27、充电使能 28 均高有效，背光 PWM 26，BQ25601 电源良好 9 低有效 |
| SDIO | Slot 1 四位。D0/D1/D2/D3/CLK/CMD 在 P4 上为 14/15/16/17/18/19，在 C6 上为 20/21/22/23/19/18 |
| 调试 | P4 USB D−/D+ 24/25、UART TX/RX 37/38。C6 UART TX/RX 16/17 |

引脚来自 A3 硬件设计交接。当前 SDIO 时钟取 20 MHz，需在样板复核。
使用独立的构建目录构建实板诊断镜像。

~~~sh
SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.hardware' \
    make a3-riscv A3_BUILD_DIR=out/a3-diagnostic
~~~

实板默认扫描附近网络。在 `GTOS Desktop Bar A3` 的 ESP-IDF 配置菜单填写
SSID 和 WPA2 口令，可以让 Desktop Orbit 加入指定网络并在 8080 端口提供只读状态。
菜单中的口令会编入镜像。通过 Board API 传给运行中驱动的口令使用 RAM 存储。

面板接入使用 [ST7703 组件](third_party/README.md) 和 ESP-IDF 双 lane DSI。
实板诊断配置在 GPIO 上保持 VCI、背光和充电关闭。取得 HC‑700BI3002 的确认版
初始化表、电气和 DPI 时序以及背光与首板装配数据后，可通过 CMake 变量
GTOS_A3_PANEL_PROFILE 指向定义 *gtos_a3_panel_profile* 的 C 文件。
配置字段位于 [board_panel.h](main/board_panel.h)。该路径使用 P4 LDO3 的
2.5 V D‑PHY 供电，按面板的实际扫描方向提交 1424 × 280 的应用画面。
实板面板配置可以启用 PPA 旋转和 DMA2D 帧复制。虚拟机验证覆盖到 RGB565 帧缓冲，
面板时序、硬件图形加速和背光电流仍需实板测量。

### 充电配置

单独通过 CMake 变量 `GTOS_A3_CHARGE_PROFILE` 指向定义
`gtos_a3_charge_profile` 的 C 文件，字段见 [board_charge.h](main/board_charge.h)。
构建使用实板的默认配置和新的输出目录。配置文件给出整板输入限流、快充、预充、
终止电流、标称恒压上限和 5 或 10 小时的 BQ25601 安全计时档位。
输入电流以 100 mA 为档，快充、预充和终止电流以 60 mA 为档。
A3 的 4.2 V 电池体系接受 3856–4200 mV 的标称恒压上限，软件向下选取寄存器档位。
具体档位应依据实际电池、芯片精度和整板输入能力选定。[BQ25601 数据手册](https://www.ti.com/lit/ds/symlink/bq25601.pdf)

配置通过校验、GPIO9 和寄存器均报告 Power Good、TUSB320LAI 报告 1.5 A 或 3 A
后，可通过板级 API 或 UART `c` 手动启用充电。输入限流取配置和检测到的档位中较低者。
充电参数和 CHG_CONFIG 均经回读后才拉高 GPIO28。软件维持快充和预充安全计时，关闭会令限值
回落到出厂设置的 I²C 看门狗。掉电、故障、档位低于已设限流、寄存器回读变化或总线错误
时撤销充电使能。检测到仍有明确的较低供电档位时同时收紧输入限流。USB 默认档缺少
已分配电流信息，实板充电保持关闭。
TPS63802、TLV62569、SYS 供电切换、实际电流电压和电池温升由样板电测验收。

以下命令在 ESP-EMU 验证启用实板充电路径的合成配置，同时检查默认诊断镜像的拒绝路径。
`charge_profile_fixture.c` 中的数据仅用于寄存器测试。

~~~sh
p=arch/riscv32/esp32p4
build="$PWD/out/a3-charge-fixture"
SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.hardware' \
    idf.py -C "$p" -B "$build" -D "SDKCONFIG=$build/sdkconfig" \
    -D "GTOS_A3_CHARGE_PROFILE=$PWD/$p/tests/charge_profile_fixture.c" \
    merge-bin -o "$build/gtos-a3-flash.bin"
PYTHONPATH=/path/to/esp-emulator-v1.0.0/tools \
    python3 "$p/tests/verify_charge_profile.py" --emulator /path/to/esp-emu \
    --parts /path/to/esp-emu-1.0.0-examples --build "$build" \
    --diagnostic out/a3-diagnostic --output out/a3-charge-test
cc -std=c11 -I"$p/third_party/bq25601" "$p/tests/bq25601_voltage_test.c" \
    "$p/third_party/bq25601/bq25601.c" -o out/bq25601-voltage-test
out/bq25601-voltage-test
~~~
