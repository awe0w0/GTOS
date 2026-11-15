#!/usr/bin/env python3
"""Check C6 first programming, P4 maintenance signals and an SDIO bring-up retry in ESP-EMU."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from verify_board import Guest, available_port, prepare_board
from verify_live_board import CHARGE_ENABLE, Devices, MS, PWM, VCI
from verify_radio import boot


WAKE, RF_ON, C6_RESET = 2, 5, 6
C6_FLASH_BYTES = 4 * 1024 * 1024


def wait_for_log(process, path, marker):
    deadline = time.monotonic() + 10
    while marker not in path.read_bytes():
        if process.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError(f"Expected {marker!r} in {path}")
        time.sleep(0.04)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=6)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=4)


def program_c6(args):
    out = args.output / "programmed-c6"
    out.mkdir()
    source = args.c6 / "gtos-a3-c6-flash.bin"
    flash = out / source.name
    flash.write_bytes(b"\xff" * C6_FLASH_BYTES)
    port = available_port()
    with tempfile.TemporaryDirectory(prefix="gtos-c6-rom-") as directory:
        log = Path(directory) / "rom.log"
        with log.open("xb") as output:
            rom = subprocess.Popen([str(args.emulator), "--chip", "esp32c6", "--firmware", str(flash),
                                    "--strap-mode", "0x02", "--uart-tcp", f"127.0.0.1:{port}",
                                    "--save-state", "--timeout", "120s"], stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            wait_for_log(rom, log, b"UART TCP server listening")
            command = [sys.executable, "-m", "esptool", "--chip", "esp32c6", "--port", f"socket://127.0.0.1:{port}",
                       "--before", "no-reset", "--after", "no-reset", "--no-stub", "write-flash", "0x0", str(source)]
            with (out / "write.log").open("xb") as output:
                subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=True, timeout=85)
        finally:
            stop(rom)
            shutil.copyfile(log, out / "rom.log")
    image = source.read_bytes()
    written = flash.read_bytes()
    if len(written) != C6_FLASH_BYTES or written[:len(image)] != image or any(b != 0xff for b in written[len(image):]):
        raise RuntimeError(f"C6 ROM write did not match the firmware bytes in {flash}")
    shutil.copyfile(args.c6 / "gtos-a3-c6.elf", out / "gtos-a3-c6.elf")
    return out


def verify_service(args):
    target = argparse.Namespace(emulator=args.emulator, build=args.diagnostic, parts=args.parts,
                                output=args.output / "p4-service")
    out, board = prepare_board(target)
    profile = json.loads(board.read_text())
    del profile["parts"]["wake"]
    del profile["devices"]
    board.write_text(json.dumps(profile) + "\n")
    port = available_port()
    with Guest(target, args.diagnostic / "gtos-a3-flash.bin", board, out / "service.log",
               extra=("--cosim-tcp", f"127.0.0.1:{port}")) as vm:
        with Devices(port, out / "signals.json", drive_pins=(WAKE,), watch_pins=(RF_ON, C6_RESET)) as bus:
            offset = vm.offset()
            bus.drive(WAKE, 0)
            vm.part("reset")
            vm.find("GTOS_A3_C6_SERVICE RESET_HELD", offset)
            bus.drive(WAKE, 1)
            on = bus.edge(RF_ON, 1, 0)
            vm.send("c")
            hold_ms = 350
            until = int(vm.part("part clock").split()[-1]) + hold_ms
            deadline = time.monotonic() + 6
            while int(vm.part("part clock").split()[-1]) < until:
                if time.monotonic() >= deadline:
                    raise RuntimeError("P4 stopped while holding C6 in reset")
                time.sleep(0.025)
            if any(marker in vm.uart_bytes()[offset:] for marker in
                   (b"GTOS_A3_READY", b"GTOS_A3_RADIO", b"GTOS_A3_CHARGE")):
                raise RuntimeError("P4 started dashboard activity during C6 service")

            vm.find("GTOS_A3_C6_SERVICE RESET_RELEASED", vm.send("r"))
            first_release = bus.edge(C6_RESET, 0, on)
            vm.find("GTOS_A3_C6_SERVICE RESET_RELEASED", vm.send("r"))
            reasserted = bus.edge(C6_RESET, 1, first_release)
            released = bus.edge(C6_RESET, 0, reasserted)
            vm.find("GTOS_A3_C6_SERVICE EXIT", vm.send("x"))
            off = bus.edge(RF_ON, 0, released)
            vm.find("GTOS_A3_READY console=1 usb=1", offset)
            vm.find("page=0 light=60 awake=0", vm.send("s"))
            with bus.lock:
                edges = [(pin, level) for stamp, pin, level in bus.gpio
                         if pin in (RF_ON, C6_RESET) and on <= stamp <= off]
                reset_on_entry = [level for stamp, pin, level in bus.gpio if pin == C6_RESET and stamp <= on]
                if any(level and pin in (VCI, PWM, CHARGE_ENABLE) and on <= stamp <= off
                       for stamp, pin, level in bus.gpio):
                    raise RuntimeError("A3 powered its display or charging path during C6 service")
            expected = [(RF_ON, 1), (C6_RESET, 0), (C6_RESET, 1), (C6_RESET, 0), (C6_RESET, 1), (RF_ON, 0)]
            if edges != expected or reset_on_entry[-1:] == [0] or first_release - on < hold_ms * MS \
                    or released - reasserted < 20 * MS:
                raise RuntimeError(f"Unexpected C6 RF/reset trace: {edges} in {out}")


def verify_missing_c6(args):
    target = argparse.Namespace(emulator=args.emulator, build=args.build, parts=args.parts,
                                output=args.output / "missing-c6")
    out, board = prepare_board(target)
    with Guest(target, args.build / "gtos-a3-flash.bin", board, out / "absent-c6.log") as vm:
        vm.find("GTOS_A3_READY")
        offset = vm.click("back")
        for attempt in range(2):
            log = vm.find("GTOS_A3_RADIO SDIO FAIL", offset, timeout=28)[offset:]
            if b"sdmmc_card_init failed" not in log:
                raise RuntimeError(f"SDMMC enumeration did not run during attempt {attempt + 1}")
            vm.find("GTOS_A3_WIRELESS powered=0 sdio=0", vm.send("s"))
            if attempt == 0:
                offset = vm.send("w")
        vm.finish()


def retry_radio(vm, gdb, image, port, output):
    arguments = [str(gdb), "-nx", "-q", "-batch", str(image)]
    for command in ("set confirm off", f"target remote 127.0.0.1:{port}",
                    "tbreak eh_host_bus_connect_to_slave", 'printf "SDIO_FAILURE_ARMED\\n"', "continue",
                    "backtrace 3", "return (int)-1", "detach"):
        arguments.extend(("-ex", command))
    with tempfile.TemporaryDirectory(prefix="gtos-c6-gdb-") as directory:
        log = Path(directory) / "sdio.log"
        with log.open("xb") as stream:
            debugger = subprocess.Popen(arguments, stdout=stream, stderr=subprocess.STDOUT)
        try:
            wait_for_log(debugger, log, b"SDIO_FAILURE_ARMED")
            offset = vm.click("back")
            if debugger.wait(timeout=12):
                raise RuntimeError(f"Could not inject an SDIO bring-up failure. See {output}")
        finally:
            stop(debugger)
            shutil.copyfile(log, output)
    vm.find("GTOS_A3_RADIO SDIO FAIL", offset)
    vm.find("GTOS_A3_WIRELESS powered=0 sdio=0", vm.send("s"))
    return vm.send("w")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("emulator", "build", "diagnostic", "c6", "parts", "gdb", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    verify_service(args)
    verify_missing_c6(args)
    radio = argparse.Namespace(emulator=args.emulator, build=args.build, c6=program_c6(args),
                               parts=args.parts, output=args.output / "programmed-radio")
    out, board = prepare_board(radio)
    port = available_port()

    def start_radio(guest):
        return retry_radio(guest, args.gdb, args.build / "gtos-a3.elf", port, out / "sdio-fault-gdb.log")

    boot(radio, out, board, "A3-LAB", host_extra=("--gdb", str(port)), start_radio=start_radio)
    print("A3 C6 service, blank ROM flashing, absent C6 handling and SDIO recovery passed", args.output)


if __name__ == "__main__":
    main()
