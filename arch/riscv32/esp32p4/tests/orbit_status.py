"""Read Desktop Orbit status and compare it with the wired A3 probe."""

import asyncio
from contextlib import asynccontextmanager
from http.client import HTTPConnection, HTTPException
import json
from struct import Struct
import time


SERVICE_UUID = "01d084ac-5c80-448a-aca7-8fd0072e67d4"
STATUS_UUID = "9c7c1669-67bc-493f-a1de-6ecf993ad223"
STATUS = Struct("<BBBBhhhhHBi")


def read_http_status(address, port, timeout):
    connection = HTTPConnection(address, port, timeout=timeout)
    try:
        connection.request("GET", "/status")
        response = connection.getresponse()
        if response.status != 200 or response.getheader("Content-Type") != "application/json":
            raise RuntimeError(f"Unexpected Orbit HTTP response {response.status} {response.headers}")
        body = response.read(4097)
    finally:
        connection.close()
    if len(body) > 4096:
        raise RuntimeError("Orbit HTTP response exceeds the expected status size")
    try:
        status = json.loads(body)
    except (UnicodeDecodeError, ValueError) as error:
        raise RuntimeError("Invalid Orbit HTTP status JSON") from error
    if not isinstance(status, dict):
        raise RuntimeError("Orbit HTTP status must be a JSON object")
    return body, status


def decode_gatt(data):
    if len(data) != STATUS.size:
        raise RuntimeError(f"Unexpected {len(data)}-byte Orbit GATT status")
    version, page, brightness, flags, x, y, z, gyro, current, faults, steps = STATUS.unpack(data)
    if version != 1 or page not in (0, 1, 2) or brightness > 100 or flags & ~0x0f:
        raise RuntimeError("Unsupported Orbit GATT status values")
    return dict(version=version, page=page, brightness=brightness, flags=flags, acceleration_mg=[x, y, z],
                gyro_z_dps=gyro, advertised_ma=current, charger_faults=faults, encoder=steps)


def matches_http(status, probe):
    expected = dict(page=probe["page"], brightness=probe["light"], encoder=probe["encoder"],
                    awake=probe["awake"], imu_result=probe["imu"], usb_result=probe["cc"],
                    usb_attached=probe["sink"], advertised_ma=probe["advertised"],
                    charger_result=probe["bq"], charging=probe["charge"], charge_faults=probe["faults"],
                    sdio_ready=1, wifi_online=1)
    return (all(type(status.get(name)) is int and status[name] == value for name, value in expected.items())
            and all(isinstance(status.get(name), list) and len(status[name]) == 3
                    and all(type(value) is int for value in status[name]) for name in ("accel_mg", "gyro_mdps")))


def matches_gatt(status, probe):
    flags = (bool(probe["awake"]) | ((probe["imu"] == 0) << 1)
             | ((probe["cc"] == 0 and probe["sink"] != 0) << 2)
             | ((probe["bq"] == 0 and probe["charge"] != 0) << 3))
    expected = dict(page=probe["page"], brightness=probe["light"], encoder=probe["encoder"],
                    flags=flags, advertised_ma=probe["advertised"] if probe["cc"] == 0 else 0,
                    charger_faults=probe["faults"])
    return all(status.get(name) == value for name, value in expected.items())


async def verify_stream(bench, seconds, report, name, read, matches):
    async def capture(minimum=None):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            probe = await asyncio.to_thread(bench.query, deadline)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            if (probe["page"] == 2 and probe["sdio"] == 1
                    and (minimum is None or probe["encoder"] >= minimum)):
                try:
                    status = await asyncio.wait_for(read(remaining), timeout=remaining)
                except (OSError, HTTPException, asyncio.TimeoutError):
                    pass
                else:
                    report["last_attempt"] = {"serial": probe, "remote": status}
                    if matches(status, probe):
                        return report.pop("last_attempt")
            await asyncio.sleep(0.12)
        raise RuntimeError(f"Orbit {name} status did not match the wired A3 probe")

    before = report["before"] = await capture()
    steps = before["serial"]["encoder"]
    await asyncio.to_thread(
        bench.observe, f"Turn the knob clockwise for the {name} status check, then stop", seconds,
        lambda probe: probe["input"] == 0 and probe["page"] == 2 and probe["encoder"] > steps,
    )
    report["after"] = await capture(steps + 1)


async def verify_wifi(bench, seconds, report, address, port=8080):
    report["endpoint"] = f"{address}:{port}"

    async def read(remaining):
        _, status = await asyncio.to_thread(read_http_status, address, port, min(2, remaining))
        return status

    await verify_stream(bench, seconds, report, "Wi-Fi", read, matches_http)


async def verify_ble_link(bench, seconds, report, connect, advertising):
    async with connect() as read:
        await asyncio.to_thread(bench.observe, "Wait for A3 to report the BLE connection", seconds,
                                lambda probe: probe["client"] == 1 and probe["sdio"] == 1)
        await verify_stream(bench, seconds, report, "BLE", read, matches_gatt)
    await asyncio.to_thread(bench.observe, "Wait for A3 to resume BLE advertising", seconds,
                            lambda probe: probe["ble"] == 1 and probe["client"] == 0)
    if not await advertising():
        raise RuntimeError("Could not see the selected Orbit peripheral advertise after disconnect")
    report["advertising_after_disconnect"] = True


async def verify_ble(bench, seconds, report, address):
    try:
        from bleak import BleakClient, BleakScanner
        from bleak.exc import BleakError
    except ImportError as error:
        raise RuntimeError("Install bleak on a host with a BLE adapter for --ble") from error

    async def scan():
        return await BleakScanner.find_device_by_address(address, timeout=seconds)

    @asynccontextmanager
    async def connect():
        device = await scan()
        if device is None:
            raise RuntimeError(f"The selected BLE peripheral {address} was not seen advertising")
        async with BleakClient(device, timeout=seconds) as client:
            service = client.services.get_service(SERVICE_UUID)
            characteristic = service.get_characteristic(STATUS_UUID) if service else None
            if characteristic is None or "read" not in characteristic.properties or any(
                    name in characteristic.properties for name in ("write", "write-without-response")):
                raise RuntimeError("The selected peripheral did not expose the read-only Orbit characteristic")
            async def read(_):
                data = await client.read_gatt_char(characteristic, use_cached=False)
                return dict(decode_gatt(data), payload_hex=bytes(data).hex())

            yield read

    report["address"] = address
    try:
        await verify_ble_link(bench, seconds, report, connect, scan)
    except (BleakError, asyncio.TimeoutError) as error:
        raise RuntimeError(f"BLE verification failed for {address}. {error}") from error
