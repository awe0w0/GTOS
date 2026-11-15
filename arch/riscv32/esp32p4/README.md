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

## Desktop Orbit

桌面星盘通过 IMU 的加速度和陀螺仪数据移动光点。NEXT、BACK 切换星盘、电源、无线，
WAKE 控制屏幕休眠，旋钮调亮度，按下旋钮用 NVS 保存当前页面和亮度。电源页显示
TUSB320LAI 检测到的方向和电流档位，以及 BQ25601 限流和充电状态。无线页向 C6
供电，经四位 SDIO 查询固件版本、发起 Wi-Fi 扫描并发出 GTOS Orbit BLE 广播。
该 BLE 示例使用非连接式广播。

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
| 控制 | P4 的 C6 电源 5、C6 复位 6、VCI 10、LCD 复位 27、充电使能 28 均高有效，背光 PWM 26 |
| SDIO | Slot 1 四位。D0/D1/D2/D3/CLK/CMD 在 P4 上为 14/15/16/17/18/19，在 C6 上为 20/21/22/23/19/18 |
| 调试 | P4 USB D−/D+ 24/25、UART TX/RX 37/38。C6 UART TX/RX 16/17 |

引脚来自 A3 硬件设计交接。当前 SDIO 时钟取 20 MHz，需在样板复核。
使用独立的构建目录构建实板诊断镜像。

~~~sh
SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.hardware' \
    make a3-riscv A3_BUILD_DIR=out/a3-diagnostic
~~~

面板接入使用 [ST7703 组件](third_party/README.md) 和 ESP-IDF 双 lane DSI。
实板诊断配置在 GPIO 上保持 VCI、背光和充电关闭。取得 HC‑700BI3002 的确认版
初始化表、电气和 DPI 时序以及背光与首板装配数据后，可通过 CMake 变量
GTOS_A3_PANEL_PROFILE 指向定义 *gtos_a3_panel_profile* 的 C 文件。
配置字段位于 [board_panel.h](main/board_panel.h)。该路径使用 P4 LDO3 的
2.5 V D‑PHY 供电，按面板的实际扫描方向提交 1424 × 280 的应用画面。
实板面板配置可以启用 PPA 旋转和 DMA2D 帧复制。虚拟机验证覆盖到 RGB565 帧缓冲，
面板时序、硬件图形加速和背光电流仍需实板测量。
同一实板配置将充电保持关闭，待电芯规格、USB 枚举与输入限流一并确认。
TPS63802、TLV62569 和电池温升由样板电测验收。
