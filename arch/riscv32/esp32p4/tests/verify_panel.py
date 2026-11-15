#!/usr/bin/env python3
"""Check A3 DBI packets and the panel lifecycle with injected hardware in ESP-EMU."""

import argparse
import json
from pathlib import Path

from verify_board import Guest, available_port
from verify_live_board import Devices, MS, PWM, RESET, VCI


def display_cycles(bus, count):
    with bus.lock:
        signals = list(bus.gpio)
    rails = [(time, pin, level) for time, pin, level in signals if pin in (VCI, RESET)]
    first_on = next((index for index, (_, pin, level) in enumerate(rails) if (pin, level) == (VCI, 1)), None)
    if first_on is None:
        raise RuntimeError("VCI did not turn on")
    cycles = rails[first_on:]
    sequence = [(VCI, 1), (RESET, 0), (RESET, 1), (VCI, 0)]
    if [(pin, level) for _, pin, level in cycles] != sequence * count:
        raise RuntimeError(f"Unexpected VCI/reset sequence: {cycles}")
    if any(pin == PWM and level and time >= cycles[0][0] for time, pin, level in signals):
        raise RuntimeError("Diagnostic backlight rose while the panel was powered")
    if not any(pin == PWM and not level for _, pin, level in signals):
        raise RuntimeError("The GPIO trace did not show the backlight parked low")
    return cycles


def verify_power(bus, output):
    cycles = display_cycles(bus, 3)
    setup = [(cycles[index + 1][0] - cycles[index][0]) / MS for index in (0, 4, 8)]
    dwell = [(cycles[index + 1][0] - cycles[index][0]) / MS for index in (3, 7)]
    if any(delay < 20 for delay in setup) or any(delay < 50 for delay in dwell):
        raise RuntimeError(f"Unexpected VCI delays: reset={setup}, off={dwell}")
    if cycles[7][0] - cycles[4][0] < 500 * MS:
        raise RuntimeError("VCI was cut before the pending DMA transfer completed")
    output.write_text(json.dumps({"vci_to_reset_release_ms": setup, "backlight_high_edges": 0,
                                  "vci_off_ms": dwell, "three_cycles_ns": cycles}) + "\n")


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    profile = args.output / "gpio-board.json"
    profile.write_text('{"parts": {}}\n')
    port = available_port()
    flash = args.build / "gtos-a3-panel-test-flash.bin"
    with Guest(args, flash, profile, args.output / "panel.log", elf_name="gtos-a3-panel-test.elf",
               extra=("--cosim-tcp", f"127.0.0.1:{port}")) as vm:
        with Devices(port, args.output / "signals.json") as bus:
            offset = vm.offset()
            vm.part("reset")
            vm.find("GTOS_A3_PANEL_TEST DBI_PACKETS PASS", offset)
            vm.find("GTOS_A3_PANEL_TEST INIT_ROLLBACK PASS", offset)
            vm.find("GTOS_A3_PANEL_TEST DMA_AND_DELETE_RETRY PASS", offset)
            vm.find("GTOS_A3_PANEL_TEST PASS", offset)
            bus.wait(lambda: sum(pin == VCI and level == 1 for _, pin, level in bus.gpio) >= 3,
                     "the third VCI assertion")
            with bus.lock:
                last_on = max(time for time, pin, level in bus.gpio if pin == VCI and level == 1)
            bus.edge(VCI, 0, last_on)
            verify_power(bus, args.output / "power-sequence.json")
    print("A3 DBI packets, dark startup, ST7703 init rollback, DMA retirement and restart passed:", args.output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
