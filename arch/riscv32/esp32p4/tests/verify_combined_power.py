#!/usr/bin/env python3
"""Check charging protection during a native display wake with C6 Wi-Fi online."""

import argparse
import hashlib
import json
from pathlib import Path

from verify_board import available_port, prepare_board
from verify_charge_profile import ChargerDevices, PG_N, enabled
from verify_live_board import CC_IRQ_N, CHARGER, CHARGE_ENABLE, MS, RESET, VCI
from verify_radio import expect_status, linked_guests


PLL_TIMEOUT = "D-PHY PLL lock timed out"


class WakeFaultDevices(ChargerDevices):
    def __init__(self, port, output):
        super().__init__(port, output, initial_limit=200)
        self.pending_fault = None
        self.stimulus = None

    def fail_on_reset_release(self, inject):
        with self.lock:
            self.pending_fault = inject
            self.stimulus = None

    def gpio_changed(self, pin, level):
        if pin == RESET and level == 0:
            with self.lock:
                fault = self.pending_fault
                self.pending_fault = None
            if fault:
                stamp = fault()
                with self.lock:
                    self.stimulus = stamp


def interrupt_while_waking(vm, bus, out, station_port, name, inject):
    bus.drive(PG_N, 0)
    bus.drive(CC_IRQ_N, 1)
    bus.usb(attached=True, power_good=True, current_mode=3)
    enabled(vm, bus, 1800)
    expect_status(station_port, out / f"{name}-before.json", {
        "page": 2, "awake": 0, "charging": 1, "sdio_ready": 1, "wifi_online": 1,
    })

    now = bus.now()
    offset = vm.offset()
    bus.fail_on_reset_release(inject)
    vm.part("part wake input press")
    vm.part("part wake input release after 180")
    powered = bus.edge(VCI, 1, now)
    released = bus.edge(RESET, 0, powered)
    stimulus = bus.wait(lambda: bus.stimulus, f"{name} at display reset release")
    cut = bus.edge(CHARGE_ENABLE, 0, stimulus)
    rail_off = bus.edge(VCI, 0, released)
    bus.drive(CC_IRQ_N, 1)
    bus.wait(lambda: not bus.registers[CHARGER][1] & 0x10, "BQ25601 CHG_CONFIG cleared")
    vm.find(PLL_TIMEOUT, offset)
    vm.find("page=2 light=60 awake=0", offset)
    expect_status(station_port, out / f"{name}-after.json", {
        "page": 2, "awake": 0, "charging": 0, "sdio_ready": 1, "wifi_online": 1,
    })
    # Let the application's debounce finish after the display wait.
    vm.find("input=0 keys=0", vm.send("i"))
    result = dict(stimulus_ns=stimulus, charge_cut_ns=cut, display_rail_on_ns=powered,
                  display_reset_released_ns=released, display_rail_off_ns=rail_off,
                  charge_cut_delay_ms=(cut - stimulus) / MS, cut_during_wake=released <= stimulus < cut < rail_off)
    print(f"{name}: GPIO28 fell after {result['charge_cut_delay_ms']:.1f} ms, "
          f"display rollback at {(rail_off - stimulus) / MS:.1f} ms", flush=True)
    return result


def run(args):
    out, board = prepare_board(args)
    profile = json.loads(board.read_text())
    del profile["devices"]
    board.write_text(json.dumps(profile) + "\n")
    cosim = available_port()
    results = {}
    report = {"transport": "emulator_uart_tcp", "profiles": "synthetic",
              "elf_sha256": hashlib.sha256((args.build / "gtos-a3.elf").read_bytes()).hexdigest(),
              "faults": results}
    with linked_guests(args, out, board, "A3-LAB", host_extra=("--cosim-tcp", f"127.0.0.1:{cosim}")) as (vm, port):
        with WakeFaultDevices(cosim, out / "signals.json") as bus:
            bus.drive(PG_N, 0)
            bus.drive(CC_IRQ_N, 1)
            offset = vm.offset()
            vm.part("reset")
            vm.find("GTOS_A3_CORE PASS psram=16777216 checks=0", offset)
            vm.find(PLL_TIMEOUT, offset)
            vm.find("GTOS_A3_READY console=1 usb=1", offset)
            vm.find("GTOS_A3_PROBE v=1 sim=0 panel=1 charging_profile=1", vm.send("i"))
            vm.find("GTOS_A3_ORBIT HTTP READY port=8080", vm.click("back"), timeout=25)
            for name, inject in (
                ("power-good", lambda: bus.drive(PG_N, 1)),
                ("cc-downgrade", lambda: bus.cc_change_during_ack(1)),
                ("charger-fault", lambda: bus.fault(0x08)),
            ):
                results[name] = interrupt_while_waking(vm, bus, out, port, name, inject)
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    if any(not result["cut_during_wake"] for result in results.values()):
        raise RuntimeError(f"Charging protection waited for the display rollback. See {out / 'report.json'}")
    print("Charging protection ran during the native display wait with C6 Wi-Fi online", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("emulator", "build", "c6", "parts", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
