#!/usr/bin/env python3
"""Check A3 DBI packets and the panel lifecycle with injected hardware in ESP-EMU."""

import argparse
import json
from pathlib import Path

from verify_board import Guest, available_port
from verify_live_board import Devices, MS, PWM, RESET, VCI


SCANOUT_MARKER = 30  # GPIO used only by the synthetic backlight fixture.


def display_cycles(bus, count, *, dark=True):
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
    if dark and any(pin == PWM and level and time >= cycles[0][0] for time, pin, level in signals):
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


def verify_first_frame(bus, output):
    cycles = display_cycles(bus, 2, dark=False)
    with bus.lock:
        markers = [stamp for stamp, pin, level in bus.gpio if pin == SCANOUT_MARKER and level]
        light = [stamp for stamp, pin, level in bus.gpio if pin == PWM and level and stamp >= cycles[0][0]]
    if len(markers) != 1 or not light or not cycles[1][0] < markers[0] <= light[0] <= light[-1] < cycles[3][0]:
        raise RuntimeError(f"Backlight active outside the verified scanout interval. See {bus.output}")
    bus.pwm(markers[0], 75)
    output.write_text(json.dumps({"second_fresh_vsync_ns": markers[0], "first_pwm_high_ns": light[0],
                                  "pwm_percent": 75, "vci_cycles_ns": cycles}, indent=2) + "\n")


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    profile = args.output / "gpio-board.json"
    profile.write_text('{"parts": {}}\n')
    port = available_port()
    flash = args.build / "gtos-a3-panel-test-flash.bin"
    extra = ["--cosim-tcp", f"127.0.0.1:{port}"]
    with Guest(args, flash, profile, args.output / "panel.log", elf_name="gtos-a3-panel-test.elf",
               extra=extra) as vm:
        with Devices(port, args.output / "signals.json",
                     watch_pins=(SCANOUT_MARKER,) if args.backlight else ()) as bus:
            offset = vm.offset()
            vm.part("reset")
            vm.find("GTOS_A3_PANEL_TEST DBI_PACKETS PASS", offset)
            if args.ppa_sdk:
                vm.find("GTOS_A3_PANEL_TEST SDK_PPA_RETURN result=0", offset)
                vm.find("PPA rotation timed out", offset)
                vm.find("GTOS_A3_PANEL_TEST SDK_PPA_TIMEOUT_FALLBACK PASS", offset)
                count = 1
            elif args.dma2d_unavailable:
                vm.find("DMA2D pool unavailable (ESP_ERR_TIMEOUT), using CPU frames", offset)
                vm.find("GTOS_A3_PANEL_TEST DMA2D_MMIO_FALLBACK PASS", offset)
                vm.find("GTOS_A3_PANEL_TEST DMA2D_RX_FALLBACK PASS", offset)
                vm.find("GTOS_A3_PANEL_TEST DMA2D_LANDSCAPE_FALLBACK PASS", offset)
                count = 2
            elif args.backlight:
                vm.find("GTOS_A3_PANEL_TEST BEFORE_FRAME duty=0", offset)
                vm.find("GTOS_A3_PANEL_TEST DRAW_AND_COPY_FAILURE_DARK PASS", offset)
                vm.find("GTOS_A3_PANEL_TEST INCOMPLETE_SCANOUT_DARK PASS", offset)
                vm.find("GTOS_A3_PANEL_TEST FIRST_FRAME_BACKLIGHT PASS", offset)
                count = 2
            elif args.ppa_recovery:
                vm.find("GTOS_A3_PANEL_TEST PPA_TIMEOUT_CPU_COPY PASS", offset)
                vm.find("GTOS_A3_PANEL_TEST PPA_LATE_COMPLETION_RECOVERY PASS", offset)
                count = 2
            else:
                vm.find("GTOS_A3_PANEL_TEST INIT_ROLLBACK PASS", offset)
                vm.find("GTOS_A3_PANEL_TEST DMA_AND_DELETE_RETRY PASS", offset)
                count = 3
            if not args.ppa_sdk:
                vm.find("GTOS_A3_PANEL_TEST PASS", offset)
            bus.wait(lambda: sum(pin == VCI and level == 1 for _, pin, level in bus.gpio) >= count,
                     f"{count} VCI assertions")
            with bus.lock:
                last_on = max(time for time, pin, level in bus.gpio if pin == VCI and level == 1)
            bus.edge(VCI, 0, last_on)
            if args.ppa_sdk:
                cycles = display_cycles(bus, 1)
                (args.output / "sdk-ppa.json").write_text(json.dumps({
                    "dma2d_registers": "fixture", "debugger": False, "sdk_submission_returned": True,
                    "ppa_irq_withheld": True, "software_copy": True, "vci_cycles_ns": cycles,
                }, indent=2) + "\n")
            elif args.dma2d_unavailable:
                cycles = display_cycles(bus, 2)
                (args.output / "dma2d-unavailable.json").write_text(json.dumps({
                    "emulator_mmio": "timeout", "fixture_rx_only": "timeout",
                    "software_frames": ["portrait", "landscape"], "vci_cycles_ns": cycles,
                }, indent=2) + "\n")
            elif args.backlight:
                verify_first_frame(bus, args.output / "first-frame.json")
            elif args.ppa_recovery:
                cycles = display_cycles(bus, 2)
                (args.output / "ppa-recovery.json").write_text(json.dumps({
                    "vci_cycles_ns": cycles, "software_copy_while_pending": True,
                    "late_completion_and_reenable": True,
                }, indent=2) + "\n")
            else:
                verify_power(bus, args.output / "power-sequence.json")
    if args.ppa_sdk:
        print("A3 ESP-IDF PPA submission with fixture DMA2D readiness and no completion passed:", args.output)
    elif args.dma2d_unavailable:
        print("A3 DMA2D readiness timeout, CPU fallback, and power cycling passed:", args.output)
    elif args.backlight:
        print("A3 first frame and PWM sequencing, rejection, and restart passed:", args.output)
    elif args.ppa_recovery:
        print("A3 PPA timeout, software rotation, power cycling, and late completion passed:", args.output)
    else:
        print("A3 DBI packets, dark startup, ST7703 init rollback, DMA retirement and restart passed:", args.output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--backlight", action="store_true", help="Run the GTOS_A3_TEST_BACKLIGHT fixture")
    mode.add_argument("--ppa-recovery", action="store_true", help="Run the GTOS_A3_TEST_PPA_RECOVERY fixture")
    mode.add_argument("--ppa-sdk", action="store_true", help="Run GTOS_A3_TEST_REAL_PPA with fixture DMA2D registers")
    mode.add_argument("--dma2d-unavailable", action="store_true", help="Run GTOS_A3_TEST_DMA2D_UNAVAILABLE")
    run(parser.parse_args())


if __name__ == "__main__":
    main()
