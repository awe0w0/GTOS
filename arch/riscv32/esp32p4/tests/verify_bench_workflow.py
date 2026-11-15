#!/usr/bin/env python3
"""Exercise the bare-board acceptance CLI against the native A3 firmware in ESP-EMU."""

import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

from verify_board import available_port, control_command, prepare_board
from verify_live_board import Devices


FLIP = b"Unplug J1, flip"
SAVE = b"Press and release the knob"
RESET = b"Press RESET (SW5)"
PG_N = 9


@contextmanager
def capture(command, log):
    with log.open("xb") as stream:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT)
    try:
        yield process
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def wait_text(process, log, marker, start=0):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if log.exists():
            position = log.read_bytes().find(marker, start)
            if position >= 0:
                return position + len(marker)
        if process.poll() is not None:
            break
        time.sleep(0.03)
    raise RuntimeError(f"Expected {marker!r} in {log}")


def command(args, uart, output, *options):
    return [sys.executable, str(Path(__file__).with_name("verify_hardware.py")), "--tcp", str(uart),
            "--output", str(output), "--elf", str(args.build / "gtos-a3.elf"), "--step-seconds", "6", *options]


def report(process, path, expected):
    if process.wait(timeout=18) != (0 if expected == "passed" else 1):
        raise RuntimeError(f"Unexpected verification exit code. See {path}")
    data = json.loads(path.read_text())
    if data["transport"] != "emulator_uart_tcp" or data["result"] != expected:
        raise RuntimeError(f"Unexpected verification report {path}")
    return data


def exercise(args, out, uart, control, devices):
    def click(part):
        control_command(control, f"part {part} input press")
        control_command(control, f"part {part} input release after 180")

    log, target = out / "bench.log", out / "bench"
    with capture(command(args, uart, target, "--usb-c", "--expect-current", "1500", "--settings"), log) as test:
        wait_text(test, log, FLIP)
        devices.usb(attached=False, power_good=False)
        devices.drive(PG_N, 1)
        wait_text(test, target / "console.log", b"cc=0 sink=0 flipped=0 advertised=0 bq=0 pg=0")
        devices.usb(attached=True, power_good=True)
        devices.drive(PG_N, 0)
        control_command(control, "reset")
        wait_text(test, target / "console.log", b"cc=0 sink=1 flipped=0 advertised=500 bq=0 pg=1")
        devices.usb(attached=True, power_good=True, current_mode=1)
        wait_text(test, log, b"Select page 1")
        click("next")
        wait_text(test, log, SAVE)
        serial = target / "console.log"
        before_press = serial.stat().st_size
        control_command(control, "part encoder input press")
        held = wait_text(test, serial, b"keys=8", before_press)
        wait_text(test, serial, b"keys=8", held)
        if RESET in log.read_bytes():
            raise RuntimeError("Bench requested a reset while the knob was still held")
        control_command(control, "part encoder input release")
        wait_text(test, log, RESET)
        control_command(control, "reset")
        restoring = wait_text(test, log, b"Select page 0")
        click("back")
        wait_text(test, log, SAVE, restoring)
        click("encoder")
        data = report(test, target / "report.json", "passed")
    cable, settings = data["usb_c"], data["settings"]
    if (cable["first"]["flipped"], cable["reversed"]["flipped"]) != (1, 0):
        raise RuntimeError("Bench did not capture the two J1 orientations")
    if any((cable[name]["advertised"], cable[name]["pg"]) != (1500, 1) for name in ("first", "reversed")):
        raise RuntimeError("Bench accepted the wrong CC current setting or Power Good state")
    if settings["after_reset"]["page"] != 1 or "result=0 page=0" not in settings["restored"]:
        raise RuntimeError("Bench did not capture the saved page and restoration")

    control_command(control, "reset")
    target = out / "restored"
    with capture(command(args, uart, target), out / "restored.log") as test:
        if report(test, target / "report.json", "passed")["initial"]["page"] != 0:
            raise RuntimeError("The restored page did not survive another reset")

    log, target = out / "same-orientation.log", out / "same-orientation"
    with capture(command(args, uart, target, "--usb-c"), log) as test:
        wait_text(test, log, FLIP)
        control_command(control, "reset")
        data = report(test, target / "report.json", "failed")
    if "flip its plug" not in data["error"] or "reversed" in data["usb_c"]:
        raise RuntimeError("The bench accepted a reset with the same J1 orientation")

    log, target = out / "no-reset.log", out / "no-reset"
    with capture(command(args, uart, target, "--settings"), log) as test:
        wait_text(test, log, b"Select page 1")
        click("next")
        wait_text(test, log, SAVE)
        click("encoder")
        wait_text(test, log, RESET)
        data = report(test, target / "report.json", "failed")
    if "Press RESET" not in data["error"] or "after_reset" in data["settings"]:
        raise RuntimeError("The bench treated an unchanged running session as a reboot")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("emulator", "build", "parts", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    out, board = prepare_board(args)
    profile = json.loads(board.read_text())
    del profile["devices"]
    board.write_text(json.dumps(profile) + "\n")
    flash = out / "gtos-a3-flash.bin"
    shutil.copyfile(args.build / flash.name, flash)
    ports = set()
    while len(ports) < 3:
        ports.add(available_port())
    uart, control, cosim = ports
    command = [str(args.emulator.resolve()), "--chip", "esp32p4", "--psram-size", "16M",
               "--firmware", str(flash), "--elf", str(args.build / "gtos-a3.elf"),
               "--cosim-board", str(board), "--cosim-tcp", f"127.0.0.1:{cosim}",
               "--uart-tcp", f"127.0.0.1:{uart}", "--control-tcp", f"127.0.0.1:{control}",
               "--timeout", "90s"]
    log = out / "emulator.log"
    with capture(command, log) as emulator:
        wait_text(emulator, log, b"Starting emulation")
        with Devices(cosim, out / "signals.json", drive_pins=(PG_N,)) as devices:
            devices.drive(PG_N, 0)
            control_command(control, "reset")
            exercise(args, out, uart, control, devices)
    print("A3 bench USB-C, settings restore and rejection checks passed", out)


if __name__ == "__main__":
    main()
