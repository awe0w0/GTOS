#!/usr/bin/env python3
"""Drive A3 I2C devices at runtime and observe ESP32-P4 GPIO and LEDC output."""

import argparse
import json
from pathlib import Path
import select
import socket
import struct
import threading
import time

# ESP-EMU v1.0.0 tools/cosim_tcp.py, supplied through PYTHONPATH.
from cosim_tcp import accepted, connect, decode, field, gpio_claim, gpio_drive, ld, recv, send, varint

from verify_board import Guest, available_port, prepare_board


CC, IMU, CHARGER = 0x47, 0x6A, 0x6B
VCI, PWM, RESET, CHARGE_ENABLE, CC_IRQ_N = 10, 26, 27, 28, 4
MS = 1_000_000


def number(tag, value):
    return field(tag, varint(value))


class Devices:
    def __init__(self, port, output, drive_pins=(), watch_pins=()):
        self.port, self.output = port, output
        self.drive_pins = drive_pins
        self.watch_pins = watch_pins
        self.lock = threading.Lock()
        self.send_lock = threading.Lock()
        self.closing = threading.Event()
        self.clock = 0
        self.error = None
        self.offline = set()
        self.gpio, self.i2c, self.charge_starts = [], [], []
        self.stimuli = []
        self.registers = {address: bytearray(256) for address in (CC, IMU, CHARGER)}
        self.pointer = dict.fromkeys(self.registers, 0)
        self.registers[CC][:8] = bytes.fromhex("30 32 33 42 53 55 54 00")
        self.registers[IMU][0x0F] = 0x6A
        self.registers[CHARGER][0x00] = 0x01
        self.registers[CHARGER][0x0B] = 0x11
        self.motion((1200, -400, 16384), (100, -100, 0))
        self.usb(attached=True, power_good=True, flipped=True, current_mode=1)

    def motion(self, acceleration, angular_rate):
        with self.lock:
            self.registers[IMU][0x22:0x2E] = struct.pack("<6h", *angular_rate, *acceleration)
            self.stimuli.append((self.clock, "motion", acceleration, angular_rate))

    def usb(self, *, attached, power_good, flipped=False, current_mode=0):
        with self.lock:
            cc_status = ((0xA0 if flipped else 0x80) if attached else 0)
            self.registers[CC][0x08:0x0A] = bytes((current_mode << 4, cc_status))
            self.registers[CHARGER][0x08] = 0x24 if power_good else 0
            self.stimuli.append((self.clock, "usb", attached, power_good, flipped, current_mode))
            return self.clock

    def fault(self, bits):
        with self.lock:
            self.registers[CHARGER][0x09] |= bits
            self.stimuli.append((self.clock, "fault", bits))
            return self.clock

    def set_online(self, address, online):
        with self.lock:
            if online:
                self.offline.discard(address)
            else:
                self.offline.add(address)
            self.stimuli.append((self.clock, "i2c-online", hex(address), online))
            return self.clock

    def drive(self, pin, level):
        if pin not in self.drive_pins:
            raise ValueError(f"GPIO{pin} was not claimed as a test input")
        with self.lock:
            stamp = self.clock
            self.stimuli.append((stamp, "gpio-drive", pin, level))
        with self.send_lock:
            send(self.socket, gpio_drive(0, pin, level))
        return stamp

    def __enter__(self):
        self.socket = connect(self.port)
        try:
            inputs = sum(1 << pin for pin in self.drive_pins)
            watch = sum(1 << pin for pin in set((VCI, PWM, RESET, CHARGE_ENABLE) + self.watch_pins)) | inputs
            claims = [gpio_claim(inputs, watch)]
            claims += [ld(2, number(1, address) + number(2, 8) + number(3, 7))
                       for address in self.registers]
            register = number(1, 1) + ld(2, b"a3-live-devices")
            send(self.socket, ld(4, register + b"".join(ld(3, claim) for claim in claims)))
            if not select.select([self.socket], [], [], 5)[0]:
                raise RuntimeError("ESP-EMU co-simulation registration timed out")
            _, ack = recv(self.socket)
            ok, reason = accepted(ack)
            if not ok:
                raise RuntimeError(f"ESP-EMU rejected A3 device claims: {reason}")
            self.reader = threading.Thread(target=self.serve, daemon=True)
            self.reader.start()
            return self
        except BaseException:
            self.socket.close()
            raise

    def __exit__(self, exc_type, *_):
        self.closing.set()
        try:
            self.socket.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.socket.close()
        self.reader.join(timeout=3)
        self.output.write_text(json.dumps({
            "gpio_columns": ["time_ns", "pin", "level"], "gpio": self.gpio,
            "i2c_columns": ["time_ns", "address", "written_hex", "read_hex"], "i2c": self.i2c,
            "charge_starts": self.charge_starts, "stimuli": self.stimuli,
        }) + "\n")
        if exc_type is None and (self.error or self.reader.is_alive()):
            raise RuntimeError("ESP-EMU co-simulation reader did not finish cleanly") from self.error

    def transfer(self, request):
        transaction = decode(request[2])
        address, written, length = transaction[2], transaction.get(3, b""), transaction.get(4, 0)
        reply = number(1, request[1]) + number(4, 1)
        if address in self.offline:
            self.i2c.append((self.clock, hex(address), written.hex(), "NACK"))
            return ld(8, reply + number(2, 0))
        regs = self.registers[address]
        if written:
            self.pointer[address] = written[0]
        index = self.pointer[address]
        if len(written) > 1:
            payload = written[1:]
            if address == CC and index == 0x09 and len(payload) == 1:
                interrupt = (regs[index] & 0x10) & ~payload[0]
                regs[index] = (regs[index] & 0xe0) | interrupt | (payload[0] & 7)
            else:
                regs[index:index + len(payload)] = payload
            index += len(payload)
        if address == IMU:
            regs[0x1E] = bool(regs[0x10] & 0xF0) | (bool(regs[0x11] & 0xF0) << 1)
        read = bytes(regs[index:index + length])
        if address == CHARGER and length and index == 0x09:
            regs[0x09] = 0  # REG09 retains the injected fault until the host reads it.
        self.pointer[address] = index + length
        self.i2c.append((self.clock, hex(address), written.hex(), read.hex()))
        return ld(8, reply + number(2, 1) + ld(3, read))

    def gpio_changed(self, pin, level):
        pass

    def serve(self):
        try:
            while True:
                _, data = recv(self.socket)
                if data is None:
                    return
                message = decode(data)
                changed = None
                with self.lock:
                    self.clock = max(self.clock, message.get(1, 0))
                    if 3 in message:
                        event = decode(message[3])
                        pin, level = event.get(1, 0), event.get(2, 0)
                        changed = (pin, level)
                        self.gpio.append((self.clock, pin, level))
                        if pin == CHARGE_ENABLE and level:
                            self.charge_starts.append((self.clock, self.registers[CHARGER][:8].hex()))
                    reply = self.transfer(decode(message[7])) if 7 in message else None
                if changed:
                    self.gpio_changed(*changed)
                if reply is not None:
                    with self.send_lock:
                        send(self.socket, reply)
        except Exception as error:
            if not self.closing.is_set():
                self.error = error

    def wait(self, condition, description):
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            with self.lock:
                if self.error:
                    raise RuntimeError("A3 live device socket failed") from self.error
                result = condition()
                if result:
                    return result
            time.sleep(0.01)
        raise RuntimeError(f"Expected {description}; see {self.output}")

    def now(self):
        with self.lock:
            return self.clock

    def edge(self, pin, level, after):
        return self.wait(lambda: next((stamp for stamp, number, value in self.gpio
                                      if stamp > after and number == pin and value == level), None),
                         f"GPIO{pin}={level} after {after / MS:.1f} ms")

    def pwm(self, after, percent):
        def measured():
            edges = [(stamp, value) for stamp, pin, value in self.gpio if pin == PWM and stamp > after]
            if len(edges) < 17:
                return False
            cycles = [(t1 - t0, t2 - t0) for (t0, a), (t1, b), (t2, c)
                      in zip(edges[-17:], edges[-16:], edges[-15:]) if (a, b, c) == (1, 0, 1)]
            return len(cycles) >= 7 and all(abs(period - MS) <= 2000
                       and abs(high / period * 100 - percent) <= 0.5 for high, period in cycles)

        self.wait(measured, f"1 kHz PWM with {percent}% duty at GPIO{PWM}")

    def power_on(self, after, brightness):
        powered = self.edge(VCI, 1, after)
        released = self.edge(RESET, 0, powered)
        lit = self.edge(PWM, 1, released)
        if released - powered < 20 * MS or lit - released < 20 * MS:
            raise RuntimeError("A3 display power, reset or backlight delay was too short")
        self.pwm(lit, brightness)

    def charge_cut(self, after, reason):
        try:
            stopped = self.edge(CHARGE_ENABLE, 0, after)
        except RuntimeError as error:
            raise RuntimeError(f"A3 did not drop charge-enable after {reason}") from error
        if stopped - after > 300 * MS:
            raise RuntimeError(f"A3 charge cut took more than 300 ms after {reason}")
        self.wait(lambda: not self.registers[CHARGER][0x01] & 0x10, "BQ25601 CHG_CONFIG cleared")
        print(f"{reason}: GPIO28 dropped after {(stopped - after) / MS:.1f} ms of guest time")


def enable_charge(vm, bus):
    vm.find("bq=0 pg=1 charge=0", vm.send("s"))
    before = bus.now()
    offset = vm.send("c")
    vm.find("GTOS_A3_CHARGE result=0", offset)
    started = bus.edge(CHARGE_ENABLE, 1, before)
    vm.find("bq=0 pg=1 charge=1 limit=500 faults=0", offset)
    with bus.lock:
        registers = bytes.fromhex(dict(bus.charge_starts)[started])
    if (registers[0] & 0x1F, registers[1] & 0x10, registers[2] & 0x3F,
            registers[4] >> 3) != (4, 0x10, 4, 10):
        raise RuntimeError("BQ25601 limits and charge control were not set before GPIO28 asserted")


def run(args):
    out, board = prepare_board(args)
    profile = json.loads(board.read_text())
    del profile["devices"]
    board.write_text(json.dumps(profile) + "\n")
    port = available_port()
    flash = args.build / "gtos-a3-flash.bin"
    with Guest(args, flash, board, out / "live.log",
               extra=("--cosim-tcp", f"127.0.0.1:{port}")) as vm:
        with Devices(port, out / "signals.json") as bus:
            started = vm.offset()
            vm.part("reset")
            vm.find("GTOS_A3_READY console=1", started)
            vm.find("imu=0 accel=73,-24,999 gyro=875,-875,0 cc=0 sink=1 flipped=1 advertised=1500", started)
            bus.power_on(0, 60)
            with bus.lock:
                settings = bus.registers[IMU][0x10:0x13]
            if settings != bytes((0x30, 0x30, 0x44)):
                raise RuntimeError("IMU was not configured for 52 Hz, auto-increment and BDU")
            rest = vm.frame(out / "orbit-rest.png", 614)
            bus.motion((-8192, 4096, 12288), (200, -200, 100))
            vm.find("imu=0 accel=-499,249,749 gyro=1750,-1750,875", vm.send("s"))
            if rest == vm.frame(out / "orbit-tilted.png", 614):
                raise RuntimeError("Changed I2C motion data did not affect the orbit framebuffer")

            before = bus.now()
            offset = vm.offset()
            vm.part("part encoder input cw")
            vm.find("page=0 light=65 awake=1 encoder=1", offset)
            bus.pwm(before, 65)
            before = bus.now()
            vm.find("awake=0", vm.click("wake"))
            asserted = bus.edge(RESET, 1, before)
            off = bus.edge(VCI, 0, asserted)
            bus.wait(lambda: bus.clock > off + 100 * MS, "100 ms with the display asleep")
            with bus.lock:
                if any(stamp > off + MS and pin == PWM and level for stamp, pin, level in bus.gpio):
                    raise RuntimeError("Backlight PWM continued while VCI was off")
            before = bus.now()
            vm.find("awake=1", vm.click("wake"))
            bus.power_on(before, 65)

            enable_charge(vm, bus)
            removed = bus.usb(attached=False, power_good=True)
            bus.charge_cut(removed, "USB-C disconnect")
            vm.find("cc=0 sink=0 flipped=0 advertised=0 bq=0 pg=1 charge=0", vm.send("s"))
            attached = bus.usb(attached=True, power_good=True, current_mode=3)
            bus.wait(lambda: bus.clock > attached + 180 * MS, "polling after USB-C reattach")
            vm.find("cc=0 sink=1 flipped=0 advertised=3000 bq=0 pg=1 charge=0", vm.send("s"))
            with bus.lock:
                if len(bus.charge_starts) != 1:
                    raise RuntimeError("A3 re-enabled charging automatically on USB-C reattach")
            enable_charge(vm, bus)
            bus.charge_cut(bus.fault(0x20), "latched BQ25601 thermal fault")
            enable_charge(vm, bus)
            bus.charge_cut(bus.usb(attached=True, power_good=False), "BQ25601 power-good loss")
            vm.find("bq=0 pg=0 charge=0 limit=500", vm.send("s"))
            bus.usb(attached=True, power_good=True)
            enable_charge(vm, bus)
            bus.charge_cut(bus.set_online(CC, False), "TUSB320LAI I2C NACK")
            vm.find("cc=-5", vm.send("s"))
            bus.set_online(CC, True)
            enable_charge(vm, bus)
            before = bus.now()
            vm.finish()
            bus.charge_cut(before, "desktop shutdown")
    print("Live A3 IMU, display sequence, measured PWM, USB-C and charge interlock passed:", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
