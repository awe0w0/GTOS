#!/usr/bin/env python3
"""Capture the A3 diagnostic firmware's UART/USB status and exercise the bench controls."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import time


PREFIX = b"GTOS_A3_PROBE "
FIELDS = set("v sim panel charging_profile rev psram flash elf page light awake input keys encoder "
             "imu accel gyro cc sink flipped advertised bq pg charge faults radio sdio scan aps ble client".split())


def parse_probe(line):
    if not line.startswith(PREFIX):
        return None
    state = {}
    try:
        for field in line[len(PREFIX):].decode("ascii").split():
            key, value = field.split("=", 1)
            if key in state:
                return None
            if key == "elf":
                if len(value) != 64 or any(character not in "0123456789abcdef" for character in value):
                    return None
                state[key] = value
            elif key in ("accel", "gyro"):
                axes = [int(axis) for axis in value.split(",")]
                if len(axes) != 3:
                    return None
                state[key] = axes
            else:
                state[key] = int(value)
    except ValueError:
        return None
    return state if state.keys() == FIELDS else None


class Bench:
    def __init__(self, port, trace):
        self.port = port
        self.trace = trace
        self.pending = bytearray()

    def query(self, deadline):
        next_send = 0.0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_send:
                if self.port.write(b"i") != 1:
                    raise OSError("Could not send the A3 status query")
                next_send = now + 0.5
            chunk = self.port.read(512)
            self.trace.write(chunk)
            self.pending.extend(chunk)
            sample = None
            while (end := self.pending.find(b"\n")) != -1:
                sample = parse_probe(self.pending[:end]) or sample
                del self.pending[:end + 1]
            if len(self.pending) > 2048:
                self.pending.clear()
            if sample is not None:
                return sample
        raise RuntimeError("No complete A3 probe reply within the deadline")

    def observe(self, prompt, seconds, condition):
        print(prompt, flush=True)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            sample = self.query(deadline)
            if condition(sample):
                return sample
            time.sleep(0.12)
        raise RuntimeError(f"Timed out waiting for {prompt}")


def check_base(state, report):
    checks = {
        "diagnostic_image": (state["v"], state["sim"], state["panel"], state["charging_profile"]) == (1, 0, 0, 0),
        "p4_revision_3": 300 <= state["rev"] < 400,
        "memory_16_mib": state["psram"] == state["flash"] == 16 * 1024 * 1024,
        "display_and_charge_reported_off": state["awake"] == state["charge"] == 0,
        "button_input": state["input"] == 0,
        "lsm6ds3tr_c": state["imu"] == 0,
        "tusb320lai": state["cc"] == 0,
        "bq25601": state["bq"] == 0,
    }
    if "expected_elf_sha256" in report:
        checks["elf_sha256"] = state["elf"] == report["expected_elf_sha256"]
    report["initial"] = state
    report["checks"] = checks
    failed = [name for name, ok in checks.items() if not ok]
    if failed:
        raise RuntimeError("A3 baseline checks failed: " + ", ".join(failed))


def exercise(bench, seconds, report):
    events = report.setdefault("events", {})
    state = bench.observe("Release the buttons and knob", seconds, lambda state: state["keys"] == 0)
    for label, mask, page_offset in (("WAKE", 1, 0), ("NEXT", 2, 1), ("BACK", 4, -1), ("knob", 8, 0)):
        expected_page = (state["page"] + page_offset) % 3
        event = bench.observe(f"Hold {label}", seconds,
                              lambda state: state["input"] == 0 and state["keys"] == mask)
        events[label] = event
        if event["page"] != expected_page:
            raise RuntimeError(f"Unexpected page {event['page']} after pressing {label}")
        state = bench.observe(f"Release {label}", seconds, lambda state: state["keys"] == 0)
    encoder = state["encoder"]
    state = bench.observe("Turn the knob clockwise", seconds, lambda state: state["encoder"] > encoder)
    events["encoder_clockwise"] = state
    encoder = state["encoder"]
    state = bench.observe("Turn the knob counterclockwise", seconds, lambda state: state["encoder"] < encoder)
    events["encoder_counterclockwise"] = state
    acceleration = state["accel"]
    events["tilt"] = bench.observe("Tilt the board", seconds, lambda state: state["imu"] == 0 and
                                  any(abs(after - before) >= 250 for after, before in zip(state["accel"], acceleration)))
    events["rotation"] = bench.observe("Rotate the board", seconds, lambda state: state["imu"] == 0 and
                                      any(abs(rate) >= 15000 for rate in state["gyro"]))


def open_port(args):
    try:
        import serial
    except ImportError as error:
        raise RuntimeError("Install pyserial in the Python environment that runs this tool") from error
    options = {"baudrate": 115200, "timeout": 0.15, "write_timeout": 1}
    if args.tcp:
        port = serial.serial_for_url(f"socket://127.0.0.1:{args.tcp}", do_not_open=True, **options)
    else:
        port = serial.Serial(**options)
        port.port = args.port
    port.dtr = False
    port.rts = False
    port.open()
    return port


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    report = {
        "transport": "emulator_uart_tcp" if args.tcp else "serial",
        "endpoint": f"127.0.0.1:{args.tcp}" if args.tcp else args.port,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "result": "failed",
    }
    try:
        if args.elf:
            report["expected_elf_sha256"] = hashlib.sha256(args.elf.read_bytes()).hexdigest()
        with (args.output / "console.log").open("xb", buffering=0) as trace, open_port(args) as port:
            bench = Bench(port, trace)
            check_base(bench.query(time.monotonic() + args.step_seconds), report)
            if args.exercise:
                exercise(bench, args.step_seconds, report)
            if args.radio:
                report["radio"] = bench.observe("Use the buttons to select RADIO", args.step_seconds,
                    lambda state: state["page"] == 2 and all(state[name] == 1 for name in ("radio", "sdio", "scan", "ble")))
        report["result"] = "passed"
    except (OSError, RuntimeError, KeyboardInterrupt) as error:
        report["error"] = str(error) or "Interrupted"
    finally:
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if report["result"] == "failed":
        print(report["error"], file=sys.stderr)
        return 1
    print("A3 diagnostic checks passed", args.output)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Serial port for J1 USB Serial/JTAG or TP2 UART0")
    source.add_argument("--tcp", type=int, help="Local ESP-EMU UART port")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--elf", type=Path, help="Verify the reported digest against this firmware ELF")
    parser.add_argument("--exercise", action="store_true", help="Exercise the keys, encoder and motion sensor")
    parser.add_argument("--radio", action="store_true", help="Check the C6 response, scan completion and BLE advertising")
    parser.add_argument("--step-seconds", type=int, default=30)
    args = parser.parse_args()
    if args.step_seconds < 1 or (args.tcp is not None and not 1 <= args.tcp <= 65535):
        parser.error("use a positive step timeout and a valid TCP port")
    try:
        return run(args)
    except OSError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
