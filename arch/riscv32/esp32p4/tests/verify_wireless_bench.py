#!/usr/bin/env python3
"""Compare native A3 probes with independent Wi-Fi and BLE reads in ESP-EMU."""

import argparse
import asyncio
from contextlib import asynccontextmanager
import hashlib
import json
from pathlib import Path
import time

from orbit_status import decode_gatt, matches_http, read_http_status, verify_ble_link, verify_stream, verify_wifi
from verify_ble import connect_to_orbit, resumed_advertisement, virtual_central
from verify_board import available_port, prepare_board
from verify_hardware import Bench, check_base
from verify_radio import linked_guests


class GuestUart:
    def __init__(self, guest):
        self.guest = guest
        self.received = guest.offset()

    def write(self, data):
        self.guest.send(data.decode("ascii"))
        return len(data)

    def read(self, size):
        deadline = time.monotonic() + 0.15
        while time.monotonic() < deadline:
            data = self.guest.uart_bytes()[self.received:self.received + size]
            if data:
                self.received += len(data)
                return data
            time.sleep(0.01)
        return b""


class VirtualBench(Bench):
    def __init__(self, guest, trace):
        super().__init__(GuestUart(guest), trace)
        self.guest = guest

    def observe(self, prompt, seconds, condition):
        if prompt.startswith("Turn the knob clockwise"):
            self.guest.part("part encoder input cw")
        return super().observe(prompt, seconds, condition)


async def check_links(bench, port, receiver, advertisements, report):
    await verify_wifi(bench, 8, report.setdefault("wifi", {}), "127.0.0.1", port)
    stale = report["wifi"]["after"]["remote"]

    async def cached(_):
        return stale

    rejected = report.setdefault("stale_http", {})
    try:
        await verify_stream(bench, 2, rejected, "cached Wi-Fi", cached, matches_http)
    except RuntimeError as error:
        last = rejected.get("last_attempt")
        if ("did not match" not in str(error) or "before" not in rejected or not last
                or last["serial"]["encoder"] <= stale["encoder"]):
            raise
        rejected["rejected"] = True
    else:
        raise RuntimeError("Wireless bench accepted a cached pre-rotation response")

    ble = report.setdefault("ble", {})

    @asynccontextmanager
    async def connect():
        async with connect_to_orbit(receiver, advertisements) as (first, characteristic):
            ble["address"] = str(first.address)

            async def read(_):
                data = await characteristic.read_value(no_long_read=True)
                return dict(decode_gatt(data), payload_hex=bytes(data).hex())

            yield read
            _, status = await asyncio.to_thread(read_http_status, "127.0.0.1", port, 2)
            if not matches_http(status, ble["after"]["serial"]):
                raise RuntimeError("Wi-Fi status diverged while the BLE client was connected")
            report["wifi_during_ble"] = status

    async def advertising():
        advertisement = await resumed_advertisement(receiver, advertisements)
        return str(advertisement.address) == ble["address"]

    await verify_ble_link(bench, 8, ble, connect, advertising)


async def run(args):
    out, board = prepare_board(args)
    hci = available_port()
    report = {"transport": "emulator_uart_tcp", "result": "failed",
              "expected_elf_sha256": hashlib.sha256((args.build / "gtos-a3.elf").read_bytes()).hexdigest()}
    loop = asyncio.get_running_loop()
    async with virtual_central(hci) as (receiver, advertisements):
        def exercise():
            with linked_guests(args, out, board, "A3-LAB", hci) as (guest, port):
                with (out / "probe.log").open("xb", buffering=0) as trace:
                    bench = VirtualBench(guest, trace)
                    check_base(bench.query(time.monotonic() + 8), report)
                    offset = guest.click("back")
                    guest.find("GTOS_A3_ORBIT HTTP READY port=8080", offset, timeout=25)
                    guest.find("GTOS_A3_RADIO BLE PASS advertising", offset)
                    future = asyncio.run_coroutine_threadsafe(
                        check_links(bench, port, receiver, advertisements, report), loop)
                    try:
                        future.result(timeout=55)
                    finally:
                        if not future.done():
                            future.cancel()

        try:
            await asyncio.to_thread(exercise)
            report["result"] = "passed"
        finally:
            (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("A3 native Wi-Fi and GATT followed the wired encoder. Cached data was rejected.", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("emulator", "build", "c6", "parts", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
