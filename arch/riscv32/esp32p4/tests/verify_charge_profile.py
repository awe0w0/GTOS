#!/usr/bin/env python3
"""Test the hardware charging path in ESP-EMU without connecting a battery."""

import argparse
from contextlib import contextmanager
import json
from pathlib import Path

from cosim_tcp import decode
from verify_board import Guest, available_port, prepare_board
from verify_live_board import CC, CC_IRQ_N, CHARGER, CHARGE_ENABLE, Devices, MS


PG_N = 9


class ChargerDevices(Devices):
    def __init__(self, port, output):
        super().__init__(port, output, drive_pins=(PG_N, CC_IRQ_N))
        registers = self.registers[CHARGER]
        registers[0x01:0x08] = bytes((0x1A, 0xA2, 0x22, 0x58, 0x9F, 0xE6, 0x4C))
        self.default_mode = True
        self.ignore_voltage = False
        self.current_on_cc_ack = None

    def transfer(self, request):
        operation = decode(request[2])
        data = operation.get(3, b"")
        if operation[2] == CC and data == b"\x09\x15" and self.current_on_cc_ack is not None:
            self.registers[CC][0x08] = self.current_on_cc_ack << 4
            self.stimuli.append((self.clock, "cc-current-on-ack", self.current_on_cc_ack))
            self.current_on_cc_ack = None
        if operation[2] != CHARGER:
            return super().transfer(request)
        registers = self.registers[CHARGER]
        voltage = registers[4]
        if data == b"\x09" and self.default_mode:
            registers[9] |= 0x80
        reply = super().transfer(request)
        if len(data) == 2 and data[0] == 1 and data[1] & 0x40:
            self.default_mode = False
            registers[1] &= ~0x40
            registers[9] &= ~0x80
        if len(data) == 2 and data[0] == 4 and self.ignore_voltage:
            registers[4] = voltage
        return reply

    def set_voltage(self, code):
        with self.lock:
            self.registers[CHARGER][4] = code << 3
            self.stimuli.append((self.clock, "vreg-drift", code))
            return self.clock

    def set_cc_identity(self, valid):
        with self.lock:
            self.registers[CC][1:3] = b"\x32\x33" if valid else b"\x33\x32"
            self.stimuli.append((self.clock, "cc-identity", valid))
            return self.clock

    def cc_change_during_ack(self, mode):
        with self.lock:
            self.registers[CC][0x09] |= 0x15  # Pending interrupt and existing RW settings.
            self.current_on_cc_ack = mode
        return self.drive(CC_IRQ_N, 0)


@contextmanager
def start(args, build, output):
    target = argparse.Namespace(emulator=args.emulator, build=build, parts=args.parts, output=output)
    out, board = prepare_board(target)
    data = json.loads(board.read_text())
    del data["devices"]
    board.write_text(json.dumps(data) + "\n")
    port = available_port()
    with Guest(target, build / "gtos-a3-flash.bin", board, out / "charge.log",
               extra=("--cosim-tcp", f"127.0.0.1:{port}")) as vm:
        with ChargerDevices(port, out / "signals.json") as bus:
            start_offset = vm.offset()
            vm.part("reset")
            vm.find("GTOS_A3_CORE PASS psram=16777216 checks=0", start_offset)
            vm.find("GTOS_A3_READY console=1", start_offset)
            vm.find("page=0 light=60 awake=0", start_offset)
            bus.edge(CHARGE_ENABLE, 0, 0)
            bus.drive(CC_IRQ_N, 1)
            yield vm, bus


def denied(vm, bus, reason, result):
    with bus.lock:
        previous = len(bus.charge_starts)
    vm.find(f"GTOS_A3_CHARGE result={result}", vm.send("c"))
    with bus.lock:
        if len(bus.charge_starts) != previous or bus.registers[CHARGER][1] & 0x10:
            raise RuntimeError(f"P4 left charging enabled after {reason}")
    print(f"{reason}: GPIO28 stayed low and CHG_CONFIG was clear")


def enabled(vm, bus, input_ma):
    vm.find("bq=0 pg=1 charge=0", vm.send("s"))
    start_time = bus.now()
    offset = vm.send("c")
    vm.find("GTOS_A3_CHARGE result=0", offset)
    start = bus.edge(CHARGE_ENABLE, 1, start_time)
    vm.find(f"bq=0 pg=1 charge=1 limit={input_ma} faults=0", offset)
    with bus.lock:
        registers = bytes.fromhex(dict(bus.charge_starts)[start])
        if bus.default_mode:
            raise RuntimeError("GPIO28 rose before BQ25601 entered host mode")
    actual = (registers[0] & 0x1F, registers[1] & 0x70, registers[2] & 0x3F,
              registers[3], registers[4] & 0xFE, registers[5] & 0xBC, registers[7] & 0x40)
    expected = (input_ma // 100 - 1, 0x10, 6, 0x10, 0x50, 0x88, 0)
    if actual != expected:
        raise RuntimeError(f"GPIO28 rose with unexpected BQ25601 configuration: {actual}")
    print(f"GPIO28 rose after readback: {input_ma} mA input, 360 mA fast, 120 mA precharge, "
          "60 mA termination, REG04=0x50 (4176 mV nominal), 5-hour timer")


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    with start(args, args.diagnostic, args.output / "no-profile") as (vm, bus):
        bus.drive(PG_N, 0)
        bus.usb(attached=True, power_good=True, current_mode=3)
        vm.find("advertised=3000 bq=0 pg=1 charge=0", vm.send("s"))
        denied(vm, bus, "missing pack configuration", -11)

    with start(args, args.build, args.output / "configured") as (vm, bus):
        bus.drive(PG_N, 1)
        bus.usb(attached=True, power_good=True, current_mode=3)
        vm.find("advertised=3000 bq=0 pg=0 charge=0", vm.send("s"))
        denied(vm, bus, "GPIO9 high while the register reported power good", -11)

        bus.drive(PG_N, 0)
        bus.usb(attached=True, power_good=True)
        vm.find("advertised=500 bq=0 pg=1 charge=0", vm.send("s"))
        denied(vm, bus, "unallocated Default USB Power", -11)

        bus.usb(attached=True, power_good=True, current_mode=3)
        bus.set_cc_identity(False)
        vm.find("cc=-19", vm.send("s"))
        denied(vm, bus, "an unexpected CC controller advertising 3 A", -5)
        bus.set_cc_identity(True)
        vm.find("advertised=3000 bq=0 pg=1 charge=0 limit=200 faults=128", vm.send("s"))
        with bus.lock:
            bus.ignore_voltage = True
        denied(vm, bus, "failed VREG write readback", -5)
        with bus.lock:
            bus.ignore_voltage = False
        enabled(vm, bus, 1800)

        dropped = bus.usb(attached=True, power_good=True, current_mode=1)
        bus.charge_cut(dropped, "CC capability dropped from 3 A to 1.5 A")
        vm.find("advertised=1500 bq=0 pg=1 charge=0 limit=1500", vm.send("s"))
        enabled(vm, bus, 1500)
        bus.charge_cut(bus.drive(PG_N, 1), "GPIO9 power-good loss with a stale status register")
        vm.find("advertised=1500 bq=0 pg=0 charge=0", vm.send("s"))

        bus.drive(PG_N, 0)
        bus.usb(attached=True, power_good=True, current_mode=3)
        enabled(vm, bus, 1800)
        bus.charge_cut(bus.set_cc_identity(False), "TUSB320LAI signature loss with stale 3 A status")
        vm.find("cc=-19", vm.send("s"))
        bus.set_cc_identity(True)
        enabled(vm, bus, 1800)

        asserted = bus.drive(CC_IRQ_N, 0)

        def cc_samples():
            stamps = [stamp for stamp, address, sent, data in bus.i2c
                      if stamp > asserted and address == hex(CC) and not sent and len(data) == 20]
            return stamps[:3] if len(stamps) >= 3 else None

        samples = bus.wait(cc_samples, "three TUSB320 samples after GPIO4 went low")
        if samples[-1] - asserted > 80 * MS:
            raise RuntimeError("CC IRQ level did not bypass the 100 ms polling interval")
        print(f"GPIO4 low produced three CC reads in {(samples[-1] - asserted) / MS:.1f} ms")
        bus.drive(CC_IRQ_N, 1)

        notified = bus.cc_change_during_ack(1)
        bus.charge_cut(notified, "CC interrupt and capability change during acknowledgement")
        stopped = bus.edge(CHARGE_ENABLE, 0, notified)
        vm.find("advertised=1500 bq=0 pg=1 charge=0 limit=1500", vm.send("s"))
        with bus.lock:
            if bus.registers[CC][0x09] & 0x17 != 5 or bus.current_on_cc_ack is not None:
                raise RuntimeError("CC acknowledgement did not preserve the writable bits or clear the latch")
            cc_reads = [(sent, result) for stamp, address, sent, result in bus.i2c
                        if notified <= stamp <= stopped and address == hex(CC)]
        expected = [("00", ""), ("", "30323342535554003095"), ("0915", ""),
                    ("00", ""), ("", "30323342535554001085")]
        if not any(cc_reads[offset:offset + len(expected)] == expected for offset in range(len(cc_reads))):
            raise RuntimeError(f"Missing TUSB320 STOP-separated identity and IRQ read-back sequence: {cc_reads}")
        if (stopped - notified) / MS > 100:
            raise RuntimeError("GPIO4 assertion did not lead to prompt charge disable")
        bus.drive(CC_IRQ_N, 1)

        bus.usb(attached=True, power_good=True, current_mode=3)
        enabled(vm, bus, 1800)
        bus.charge_cut(bus.set_voltage(0x0B), "unexpected VREG change while charging")
    print("A3 hardware charging path passed with synthetic electrical parameters:", args.output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True, help="Hardware build with charge_profile_fixture.c")
    parser.add_argument("--diagnostic", type=Path, required=True, help="Hardware build with no charge profile")
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
