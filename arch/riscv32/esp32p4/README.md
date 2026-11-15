# GTOS on ESP32-P4

This target uses the ESP32-P4 v3.x HP RISC-V cores, 16 MiB of Hex SPI PSRAM, and
16 MiB of SPI NOR flash specified for the Desktop Bar A3. ESP-IDF handles chip
startup and provides the FreeRTOS execution environment. GTOS runs its allocator,
boot log and existing GTAPP interpreter through the RV32 local interrupt HAL
on the main task.

The same CPU HAL supplies the existing IA32 CPU-local critical sections. The
existing GTAPP package format is shared between targets. The existing PC
native SDK and Chromium workflow use the IA32 process ABI.

## Build and boot

After activating [ESP-IDF v6.1](https://github.com/espressif/esp-idf/tree/v6.1)
with the ESP32-P4 tools, run from the GTOS repository root.

~~~sh
make a3-riscv
python3 arch/riscv32/esp32p4/tests/verify_core.py \
    --emulator /path/to/esp-emu --build out/a3-riscv --output out/a3-core-test
~~~

The Make target writes *gtos-a3.elf* and *gtos-a3-flash.bin* under
*out/a3-riscv*. To run an individual boot using
[Espressif ESP-EMU 1.0.0](https://github.com/espressif/esp-emulator/releases/tag/v1.0.0):

~~~sh
esp-emu --chip esp32p4 --psram-size 16M \
    --firmware out/a3-riscv/gtos-a3-flash.bin --elf out/a3-riscv/gtos-a3.elf \
    --timeout 20s --exit-on 'GTOS_A3_CORE PASS psram=16777216 checks=0'
~~~

The test exercises RV32 interrupt save/restore across nested scopes, reads back
the GTOS boot log, verifies that GTAPP validation rejects a damaged package,
moves the paddle in the unmodified Catch package, and checks allocator
reclamation in PSRAM. It repeats the boot with a fresh VM and checks refusal
when the VM supplies 8 MiB of PSRAM.
