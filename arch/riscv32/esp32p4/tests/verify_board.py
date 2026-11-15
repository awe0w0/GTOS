#!/usr/bin/env python3
"""Exercise the GTOS dashboard with ESP-EMU's ESP32-P4 and external board devices."""

import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import threading
import time
import zlib


def available_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


class Guest:
    def __init__(self, args, flash, board, log, save=False, extra=(), elf_name="gtos-a3.elf"):
        self.log = log
        self.emulator_log = log.with_name(log.stem + "-emulator.log")
        self.received = bytearray()
        self.receive_lock = threading.Lock()
        self.port = available_port()
        uart_port = available_port()
        while uart_port == self.port:
            uart_port = available_port()
        command = [
            str(args.emulator.resolve()), "--chip", "esp32p4", "--psram-size", "16M",
            "--firmware", str(flash), "--elf", str(args.build / elf_name),
            "--cosim-board", str(board), "--control-tcp", f"127.0.0.1:{self.port}",
            "--uart-tcp", f"127.0.0.1:{uart_port}", "--timeout", "90s",
        ]
        if save:
            command.append("--save-state")
        command.extend(extra)
        with self.emulator_log.open("xb") as uart:
            self.process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=uart,
                                            stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 7
        while True:
            try:
                self.uart = socket.create_connection(("127.0.0.1", uart_port), 0.2)
                self.uart.settimeout(None)
                break
            except OSError:
                if self.process.poll() is not None or time.monotonic() >= deadline:
                    self.stop()
                    raise RuntimeError(f"ESP-EMU UART did not open. See {self.emulator_log}")
                time.sleep(0.02)
        self.log.touch(exist_ok=False)
        self.reader = threading.Thread(target=self.drain_uart, daemon=True)
        self.reader.start()

    def drain_uart(self):
        with self.log.open("ab", buffering=0) as output:
            while True:
                try:
                    chunk = self.uart.recv(16384)
                except OSError:
                    break
                if not chunk:
                    break
                output.write(chunk)
                with self.receive_lock:
                    self.received.extend(chunk)

    def offset(self):
        with self.receive_lock:
            return len(self.received)

    def uart_bytes(self):
        with self.receive_lock:
            return bytes(self.received)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.stop()
        self.uart.close()
        self.reader.join(timeout=1)

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

    def find(self, expected, start=0, timeout=20):
        deadline = time.monotonic() + timeout
        marker = expected.encode()
        while time.monotonic() < deadline:
            text = self.uart_bytes()
            if b"Guru Meditation" in text or b"PANIC" in text:
                break
            if marker in text[start:]:
                return text
            if self.process.poll() is not None:
                break
            time.sleep(0.04)
        raise RuntimeError(f"Expected {expected!r} after offset {start} in {self.log}\n"
                           + self.uart_bytes()[-1200:].decode(errors="replace"))

    def send(self, text):
        offset = self.offset()
        self.uart.sendall(text.encode())
        return offset

    def part(self, command):
        with socket.create_connection(("127.0.0.1", self.port), 3) as control:
            control.sendall((command + "\n").encode())
            with control.makefile("rb") as response:
                result = response.readline()
        if not result.startswith(b"ok"):
            raise RuntimeError(f"{command}: {result!r}")
        return result

    def click(self, name):
        offset = self.offset()
        self.part(f"part {name} input press")
        self.part(f"part {name} input release after 180")
        released = int(self.part("part clock").split()[-1]) + 220
        deadline = time.monotonic() + 5
        while int(self.part("part clock").split()[-1]) < released:
            if time.monotonic() >= deadline:
                raise RuntimeError(f"Clock stopped while releasing {name}")
            time.sleep(0.025)
        return offset

    def frame(self, target, duty):
        offset = self.send("f")
        text = self.find("GTOS_FRAME END", offset, timeout=30)[offset:]
        first = re.search(rb"GTOS_FRAME 1424 280 PWM=(\d+)\r?\n", text)
        if not first or int(first[1]) != duty:
            raise RuntimeError(f"Unexpected backlight duty in {self.log}")
        return write_png(text[first.end():text.index(b"GTOS_FRAME END")], target)

    def finish(self):
        self.find("GTOS_A3_VM_STOP", self.send("q"))
        self.stop()
        if self.process.returncode:
            raise RuntimeError(f"Emulator shutdown failed. See {self.log}")


def write_png(text, target):
    rows = [line.split()[1:] for line in text.splitlines() if line.startswith(b"GTOS_ROW ")]
    if len(rows) != 280:
        raise RuntimeError(f"Display expected 280 rows, got {len(rows)}")
    image = bytearray()
    for row in rows:
        image.append(0)
        line = bytearray()
        for run in row:
            color, count = run.split(b"/", 1)
            pixel = int(color, 16)
            count = int(count)
            if count < 1 or len(line) // 3 + count > 1424:
                raise RuntimeError(f"Invalid RGB565 run length {count}")
            red = (pixel >> 11) & 31
            green = (pixel >> 5) & 63
            blue = pixel & 31
            line.extend(bytes((red * 255 // 31, green * 255 // 63, blue * 255 // 31)) * count)
        if len(line) != 1424 * 3:
            raise RuntimeError("Incomplete display row")
        image.extend(line)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    target.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1424, 280, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(image)) + chunk(b"IEND", b"")
    )
    return zlib.crc32(image)


def prepare_board(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    board = out / "a3.json"
    profile = Path(__file__).resolve().parents[1] / "sim/a3.json"
    shutil.copyfile(profile, board)
    shutil.copyfile(args.parts / "button/pushbutton.wasm", out / "pushbutton.wasm")
    shutil.copyfile(args.parts / "encoder/ky040.wasm", out / "ky040.wasm")
    return out, board


def usb_console(args, flash, board, log):
    marker = "GTOS_A3_USB STATUS REQUEST"
    command = [
        str(args.emulator.resolve()), "--chip", "esp32p4", "--psram-size", "16M",
        "--firmware", str(flash), "--elf", str(args.build / "gtos-a3.elf"),
        "--cosim-board", str(board), "--console", "usb-jtag",
        "--inject", "s", "--inject-on", "GTOS_A3_READY console=1 usb=1",
        "--timeout", "20s", "--exit-on", marker,
    ]
    with log.open("xb") as uart:
        subprocess.run(command, stdout=uart, stderr=subprocess.STDOUT, check=True, timeout=30)
    if marker.encode() not in log.read_bytes():
        raise RuntimeError(f"USB Serial/JTAG status request did not arrive. See {log}")


def run(args):
    out, board = prepare_board(args)
    original = args.build / "gtos-a3-flash.bin"
    flash = out / "run-flash.bin"
    shutil.copyfile(original, flash)

    with Guest(args, flash, board, out / "devices.log", save=True) as vm:
        vm.find("GTOS_A3_READY console=1")
        vm.find("imu=0 accel=73,-24,999 gyro=875,-875,0 cc=0 sink=1 flipped=1 advertised=1500")
        vm.find("bq=0 pg=1 charge=0 limit=200 faults=0")
        orbit = vm.frame(out / "orbit.png", 614)
        vm.find("page=1 light=60 awake=1", vm.click("next"))
        power = vm.frame(out / "power.png", 614)
        if orbit == power:
            raise RuntimeError("NEXT did not redraw the framebuffer")
        vm.find("page=0 light=60 awake=1", vm.click("back"))
        vm.find("awake=0", vm.click("wake"))
        vm.find("GTOS_FRAME SLEEP", vm.send("f"))
        vm.find("awake=1", vm.click("wake"))
        offset = vm.offset()
        vm.part("part encoder input cw")
        vm.find("page=0 light=65 awake=1 encoder=1", offset)
        vm.frame(out / "orbit-bright.png", 665)
        vm.find("GTOS_A3_SETTINGS SAVE result=0 page=0 light=65", vm.click("encoder"))
        offset = vm.send("c")
        vm.find("GTOS_A3_CHARGE result=0", offset)
        vm.find("bq=0 pg=1 charge=1 limit=500 faults=0", offset)
        offset = vm.send("c")
        vm.find("GTOS_A3_CHARGE result=0", offset)
        vm.find("bq=0 pg=1 charge=0 limit=500 faults=0", offset)
        vm.finish()

    with Guest(args, flash, board, out / "persisted.log") as vm:
        vm.find("GTOS_A3_SETTINGS LOAD page=0 light=65")
        vm.find("GTOS_A3_READY console=1")
        offset = vm.offset()
        vm.part("part encoder input ccw")
        vm.find("page=0 light=60 awake=1 encoder=-1", offset)
        vm.finish()

    negative = json.loads(board.read_text())
    negative["devices"]["imu"]["regs"]["0x0f"] = 0
    negative["devices"]["charger"]["regs"]["0x09"] = 0x08
    negative["devices"]["usb_cc"]["regs"]["0x09"] = 0
    failure_board = out / "a3-fault.json"
    failure_board.write_text(json.dumps(negative))
    with Guest(args, original, failure_board, out / "faults.log") as vm:
        vm.find("GTOS_A3_READY console=1")
        vm.find("imu=-19")
        vm.find("cc=0 sink=0 flipped=0 advertised=0")
        vm.find("GTOS_A3_CHARGE result=-11", vm.send("c"))
        vm.finish()
    usb_console(args, original, board, out / "usb-serial.log")
    print("A3 device, input, rendering, charging, USB and cold-reboot checks passed:", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
