#!/usr/bin/env python3
"""Boot the A3 hardware configuration and inject DSI faults on wake in ESP-EMU."""

import argparse
import json
from pathlib import Path
import subprocess
import time

from verify_board import Guest, available_port, prepare_board
from verify_live_board import Devices, MS, VCI
from verify_panel import display_cycles


PLL_TIMEOUT = "D-PHY PLL lock timed out"
FAULT_LOG = {
    "lanes": "D-PHY lane idle timed out",
    "dbi-read": "DSI read response timed out for command 0x4",
    "dbi-command": "DSI command FIFO timed out",
    "dbi-write": "DSI command FIFO timed out",
}


def retry_with_fault(vm, gdb, image, port, fault, log):
    ready = "GTOS_A3_DSI_WAKE_READY"
    # The awake command runs before rendering. Use the existing app framebuffer
    # as scratch registers during that command, before the next redraw.
    # Restore the real host pointer before the driver's hardware cleanup.
    commands = [
        "set pagination off", f"target remote 127.0.0.1:{port}",
        "tbreak mipi_dsi_hal_configure_phy_pll", f'printf "{ready}\\n"', "continue",
        "set $context = (mipi_dsi_hal_context_t *)$a0", "finish",
        "set $physical_host = $context->host", "set $fixture_host = (dsi_host_dev_t *)dashboard.pixels",
        "set $fixture_host->phy_status.val = 0", "set $context->host = $fixture_host",
    ]
    if fault == "lanes":
        source = image.parent / "gtos/dsi/esp_lcd_mipi_dsi_bus.c"
        lines = source.read_text().splitlines()
        pll_delay = next(line + 1 for line, text in enumerate(lines, 1) if PLL_TIMEOUT in text)
        commands.extend((f"tbreak {source.name}:{pll_delay}", "ignore $bpnum 12", "continue"))
    else:
        commands.extend((
            "set $fixture_host->phy_status.phy_stopstateclklane = 1",
            "set $fixture_host->phy_status.phy_stopstate0lane = 1",
            "set $fixture_host->phy_status.phy_stopstate1lane = 1",
            "set $fixture_host->cmd_pkt_status.val = 0",
            "set $fixture_host->cmd_pkt_status.gen_pld_r_empty = 1",
            "set $fixture_host->gen_hdr.val = 0",
        ))
        if fault == "dbi-command":
            commands.append("set $fixture_host->cmd_pkt_status.gen_cmd_full = 1")
    commands.append("set $fixture_host->phy_status.phy_lock = 1")
    if fault != "lanes":
        # ESP-EMU leaves the self-clearing DW GDMA reset bit set during DPI creation.
        commands.extend((
            "tbreak dw_gdma_hal_init", "continue", "set $dma = (dw_gdma_hal_context_t *)$a0",
            "tbreak dw_gdma_ll_reset", "continue", "next",
            "set $dma->dev->reset0.dmac_rst = 0",
        ))
    if fault == "dbi-write":
        # Supply one response word when the driver starts waiting for BUSY and RX_EMPTY to clear.
        commands.extend((
            "tbreak panel_io_dbi_rx_param", "continue",
            "set $fixture_host->mode_cfg.cmd_video_mode = 0",
            "tbreak a3_dbi_wait if flags == 0x50", "continue",
            "set $fixture_host->gen_pld_data.val = 0x00563412",
            "set $fixture_host->cmd_pkt_status.gen_pld_r_empty = 0",
            "tbreak mipi_dsi_host_ll_gen_read_payload_fifo", "continue", "nexti",
            "set $fixture_host->cmd_pkt_status.gen_pld_r_empty = 1",
            "tbreak panel_io_dbi_tx_param", "continue",
            'printf "GTOS_A3_DBI_AFTER_READ mode=%u command=0x%x\\n", $fixture_host->mode_cfg.cmd_video_mode, $a1',
            "set $fixture_host->cmd_pkt_status.gen_cmd_full = 1",
        ))
    commands.extend((
        "tbreak mipi_dsi_hal_deinit", "continue",
        'printf "GTOS_A3_DSI_HEADER=0x%06x\\n", $fixture_host->gen_hdr.val',
        "backtrace 5", "set $context->host = $physical_host", "detach",
    ))
    argv = [str(gdb), "-nx", "-q", "-batch", str(image)]
    for command in commands:
        argv.extend(("-ex", command))
    with log.open("xb") as output:
        debugger = subprocess.Popen(argv, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            while ready.encode() not in log.read_bytes():
                if debugger.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f"GDB did not set the DSI breakpoint. See {log}")
                time.sleep(0.04)
            offset = vm.click("wake")
            if debugger.wait(timeout=15):
                raise RuntimeError(f"GDB DSI injection failed. See {log}")
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"GDB DSI injection did not finish. See {log}") from error
        finally:
            if debugger.poll() is None:
                debugger.kill()
                debugger.wait(timeout=5)
    if fault != "lanes":
        # VC 0 DCS read of ST7703 register 0x04, or no header when the command FIFO is full.
        header = "0x000000" if fault == "dbi-command" else "0x000406"
        trace = log.read_text()
        if f"GTOS_A3_DSI_HEADER={header}" not in trace or "a3_panel_stop" not in trace:
            raise RuntimeError(f"Native DBI path did not reach the expected header and teardown. See {log}")
        if fault == "dbi-write" and "GTOS_A3_DBI_AFTER_READ mode=0 command=0x36" not in trace:
            raise RuntimeError(f"Successful DBI read did not restore video mode and reach MADCTL. See {log}")
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
                offset = retry_with_fault(vm, args.gdb, args.build / "gtos-a3.elf",
                                          debug_port, args.fault, out / "dsi-gdb.log")
                vm.find(FAULT_LOG[args.fault], offset)
                if args.fault == "dbi-write":
                    vm.find("LCD ID: 12 34 56", offset)
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
            retry_is_dbi = args.gdb and args.fault != "lanes"
            retry_min = 100 if retry_is_dbi else 250
            if not 250 <= waits[0] <= 450 or not retry_min <= waits[1] <= 450:
                raise RuntimeError(f"Unexpected reset release-to-rollback times in ms: {waits}")
            if args.gdb and args.fault == "lanes" and abs(waits[1] - waits[0]) > 30:
                raise RuntimeError(f"Late PLL lock changed the startup timeout in ms: {waits}")
            (out / "power-sequence.json").write_text(json.dumps({
                "reset_release_to_rollback_ms": waits, "backlight_high_edges": 0, "two_cycles_ns": cycles,
                "wake_fault": args.fault if args.gdb else "pll",
            }) + "\n")
    print("Native DSI timeout, rail rollback, GTOS console and WAKE retry passed:", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True, help="A3 hardware build with the test panel profile")
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--gdb", type=Path, help="Use GDB to inject a DSI fault on wake")
    parser.add_argument("--fault", default="lanes", choices=FAULT_LOG)
    args = parser.parse_args()
    if args.fault != "lanes" and not args.gdb:
        parser.error("the DBI faults require --gdb")
    run(args)


if __name__ == "__main__":
    main()
