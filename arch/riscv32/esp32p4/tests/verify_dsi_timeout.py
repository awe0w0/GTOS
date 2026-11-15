#!/usr/bin/env python3
"""Boot the A3 hardware configuration and retry after a native D-PHY timeout in ESP-EMU."""

import argparse
import json
from pathlib import Path
import subprocess
import time

from verify_board import Guest, available_port, prepare_board
from verify_live_board import Devices, MS, VCI
from verify_panel import display_cycles


PLL_TIMEOUT = "D-PHY PLL lock timed out"
LANE_TIMEOUT = "D-PHY lane idle timed out"


def retry_with_lanes_busy(vm, gdb, image, port, log):
    ready = "GTOS_A3_DSI_WAKE_READY"
    source = image.parent / "gtos/dsi/esp_lcd_mipi_dsi_bus.c"
    lines = source.read_text().splitlines()
    pll_delay = next(line + 1 for line, text in enumerate(lines, 1) if PLL_TIMEOUT in text)
    # Use the allocation for portrait rotation as scratch registers during the wait.
    # Restore the real host pointer before the driver's hardware cleanup.
    commands = (
        "set pagination off", f"target remote 127.0.0.1:{port}",
        "tbreak mipi_dsi_hal_configure_phy_pll", f'printf "{ready}\\n"', "continue",
        "set $context = (mipi_dsi_hal_context_t *)$a0", "finish",
        "set $physical_host = $context->host", "set $fixture_host = (dsi_host_dev_t *)staging",
        "set $fixture_host->phy_status.val = 0", "set $context->host = $fixture_host",
        f"tbreak {source.name}:{pll_delay}", "ignore $bpnum 12", "continue",
        "set $fixture_host->phy_status.phy_lock = 1", "tbreak mipi_dsi_hal_deinit", "continue",
        "backtrace 4", "set $context->host = $physical_host", "detach",
    )
    argv = [str(gdb), "-nx", "-q", "-batch", str(image)]
    for command in commands:
        argv.extend(("-ex", command))
    with log.open("xb") as output:
        debugger = subprocess.Popen(argv, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            while ready.encode() not in log.read_bytes():
                if debugger.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f"GDB did not set the PHY breakpoint. See {log}")
                time.sleep(0.04)
            offset = vm.click("wake")
            if debugger.wait(timeout=15):
                raise RuntimeError(f"GDB PHY injection failed. See {log}")
        finally:
            if debugger.poll() is None:
                debugger.kill()
                debugger.wait(timeout=5)
    return offset


def run(args):
    out, board = prepare_board(args)
    port = available_port()
    flash = args.build / "gtos-a3-flash.bin"
    extra = ["--cosim-tcp", f"127.0.0.1:{port}"]
    if args.gdb:
        debug_port = available_port()
        while debug_port == port:
            debug_port = available_port()
        extra.extend(("--gdb", str(debug_port)))
    with Guest(args, flash, board, out / "native.log", extra=extra) as vm:
        with Devices(port, out / "signals.json") as bus:
            offset = vm.offset()
            vm.part("reset")
            vm.find("GTOS_A3_CORE PASS psram=16777216 checks=0", offset)
            vm.find(PLL_TIMEOUT, offset)
            vm.find("GTOS_A3_READY console=1 usb=1", offset)
            vm.find("page=0 light=60 awake=0", vm.send("s"))

            if args.gdb:
                offset = retry_with_lanes_busy(vm, args.gdb, args.build / "gtos-a3.elf",
                                               debug_port, out / "lane-gdb.log")
                vm.find(LANE_TIMEOUT, offset)
            else:
                offset = vm.click("wake")
                vm.find(PLL_TIMEOUT, offset)
            vm.find("page=0 light=60 awake=0", offset)
            vm.find("page=1 light=60 awake=0", vm.click("next"))
            vm.find("imu=0 accel=73,-24,999", vm.send("s"))

            first = bus.edge(VCI, 1, 0)
            retry = bus.edge(VCI, 1, first)
            bus.edge(VCI, 0, retry)
            cycles = display_cycles(bus, 2)
            waits = [(cycles[i + 2][0] - cycles[i + 1][0]) / MS for i in (0, 4)]
            if any(wait < 250 or wait > 450 for wait in waits):
                raise RuntimeError(f"Unexpected reset release-to-rollback times in ms: {waits}")
            if args.gdb and abs(waits[1] - waits[0]) > 30:
                raise RuntimeError(f"Late PLL lock changed the startup timeout in ms: {waits}")
            (out / "power-sequence.json").write_text(json.dumps({
                "reset_release_to_rollback_ms": waits, "backlight_high_edges": 0, "two_cycles_ns": cycles,
            }) + "\n")
    print("Native D-PHY timeout, rail rollback, GTOS console and WAKE retry passed:", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True, help="A3 hardware build with the test panel profile")
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--gdb", type=Path, help="Inject a locked PLL with busy lanes on retry")
    run(parser.parse_args())


if __name__ == "__main__":
    main()
