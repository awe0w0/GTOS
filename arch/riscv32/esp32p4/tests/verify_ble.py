#!/usr/bin/env python3
"""Read Desktop Orbit status through a second virtual BLE controller with Wi-Fi unavailable."""

import argparse
import asyncio
import json
from pathlib import Path
from struct import Struct

from bumble.att import ATT_Error, ErrorCode
from bumble.controller import Controller
from bumble.core import AdvertisingData, UUID
from bumble.device import Device, Peer
from bumble.hci import Address
from bumble.host import Host
from bumble.link import LocalLink
from bumble.transport import open_transport

from verify_board import available_port, prepare_board
from verify_radio import boot


SERVICE = UUID("01d084ac-5c80-448a-aca7-8fd0072e67d4")
VALUE = UUID("9c7c1669-67bc-493f-a1de-6ecf993ad223")
STATUS = Struct("<BBBBhhhhHBi")


async def read_status(characteristic, file):
    data = await characteristic.read_value(no_long_read=True)
    if len(data) != STATUS.size:
        raise RuntimeError(f"Unexpected {len(data)}-byte Orbit status")
    version, page, brightness, flags, x, y, z, gyro, current, faults, steps = STATUS.unpack(data)
    status = dict(version=version, page=page, brightness=brightness, flags=flags, acceleration_mg=[x, y, z],
                  gyro_z_dps=gyro, advertised_ma=current, charger_faults=faults, encoder=steps)
    file.write_text(json.dumps(status) + "\n")
    return status


async def exercise(receiver, advertisements, vm, out):
    first = await asyncio.wait_for(advertisements.get(), 8)
    await receiver.stop_scanning()
    mark = vm.offset()
    connection = await receiver.connect(first.address, timeout=8)
    try:
        await asyncio.to_thread(vm.find, "GTOS_A3_RADIO BLE CLIENT connected", mark)
        await asyncio.to_thread(vm.find, "station=3 ip= client=1", vm.send("s"))
        peer = Peer(connection)
        services = await peer.discover_services([SERVICE])
        characteristics = await services[0].discover_characteristics([VALUE]) if services else []
        if len(characteristics) != 1:
            raise RuntimeError("Orbit status characteristic not discovered")
        characteristic = characteristics[0]
        before = await read_status(characteristic, out / "ble-status.json")
        expected = dict(version=1, page=2, brightness=60, flags=7, acceleration_mg=[73, -24, 999],
                        gyro_z_dps=0, advertised_ma=1500, charger_faults=0, encoder=0)
        if before != expected:
            raise RuntimeError(f"Unexpected initial GATT sample: {before}")
        await asyncio.to_thread(vm.frame, out / "ble-connected.png", 614)

        mark = vm.offset()
        await asyncio.to_thread(vm.part, "part encoder input cw")
        await asyncio.to_thread(vm.find, "page=2 light=65 awake=1 encoder=1", mark)
        after = await read_status(characteristic, out / "ble-status-after-knob.json")
        if after != dict(expected, brightness=65, encoder=1):
            raise RuntimeError(f"GATT sample did not follow the encoder and backlight: {after}")

        try:
            await characteristic.write_value(b"\0", with_response=True)
        except ATT_Error as error:
            if error.error_code != ErrorCode.WRITE_NOT_PERMITTED:
                raise
        else:
            raise RuntimeError("Orbit allowed a write to its read-only status characteristic")
    finally:
        mark = vm.offset()
        await connection.disconnect()
    await asyncio.to_thread(vm.find, "GTOS_A3_RADIO BLE PASS advertising", mark)
    while not advertisements.empty():
        advertisements.get_nowait()
    await receiver.start_scanning(legacy=True, active=False)
    try:
        resumed = await asyncio.wait_for(advertisements.get(), 8)
    finally:
        await receiver.stop_scanning()
    if resumed.address != first.address:
        raise RuntimeError("A different device appeared after disconnect")
    (out / "ble-received.json").write_text(json.dumps(dict(
        name="GTOS Orbit", address=str(first.address), payload=first.data_bytes.hex(),
        write_rejected=True, resumed_after_disconnect=True,
    )) + "\n")


async def run(args):
    out, board = prepare_board(args)
    port = available_port()
    local_link = LocalLink()
    async with await open_transport(f"tcp-server:127.0.0.1:{port}") as transport:
        address = "02:00:00:00:00:C6"
        c6 = Controller("A3 C6", host_source=transport.source, host_sink=transport.sink,
                        link=local_link, public_address=address)
        # Bumble's LocalLink uses the random-address slot to look up ACL peers.
        c6.random_address = Address(address, Address.PUBLIC_DEVICE_ADDRESS)
        phone = Controller("Orbit receiver", link=local_link, public_address="02:00:00:00:00:01")
        receiver = Device(name="Orbit receiver", host=Host(controller_source=phone, controller_sink=phone))
        advertisements = asyncio.Queue(maxsize=1)

        def observe(report):
            if (report.is_connectable and report.data_bytes.startswith(b"\x02\x01\x06")
                    and report.data.get(AdvertisingData.COMPLETE_LOCAL_NAME) == "GTOS Orbit"
                    and advertisements.empty()):
                advertisements.put_nowait(report)

        receiver.on("advertisement", observe)
        await receiver.power_on()
        await receiver.start_scanning(legacy=True, active=False)
        loop = asyncio.get_running_loop()

        def check(vm):
            future = asyncio.run_coroutine_threadsafe(exercise(receiver, advertisements, vm, out), loop)
            try:
                future.result(timeout=35)
            finally:
                if not future.done():
                    future.cancel()

        await asyncio.to_thread(boot, args, out, board, "A3-OTHER", port, check)
    print("A3 BLE connection, live GATT read, write rejection and resumed advertising passed:", out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--c6", type=Path, required=True)
    parser.add_argument("--parts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
