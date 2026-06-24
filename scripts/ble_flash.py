#!/usr/bin/env python3
# Drop firmware.bin onto the watch's SD card over BLE. The watch's USB
# INSTALL app then flashes from SD — same flow as USB-MSC, just no cable.
#
#   ./ble_flash.py [path/to/firmware.bin] [--name smartwatch]
#
# Requires: pip install bleak
#
# The watch must already be paired to this host (BLE whitelist is on once
# any bond exists). Pair once via bluetoothctl, then this script just works.

import argparse, asyncio, struct, sys, time
from pathlib import Path

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    sys.exit("pip install bleak")

SVC  = "f00d0000-1c8f-4e0a-9d3c-4b6e4f5a8b00"
CTL  = "f00d0001-1c8f-4e0a-9d3c-4b6e4f5a8b00"
DATA = "f00d0002-1c8f-4e0a-9d3c-4b6e4f5a8b00"

# Conservative chunk: MTU - 3 (ATT header). Watch negotiates 517 with most
# stacks but BlueZ on Linux often lands at 247 → 244 payload. 200 is safe.
CHUNK = 200

async def find(name: str, timeout: float) -> str:
    print(f"scanning for '{name}'...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, _ad: (d.name or "") == name, timeout=timeout)
    if not dev:
        sys.exit(f"no device named '{name}' found")
    print(f"found {dev.address}")
    return dev.address

async def flash(addr: str, fw: bytes):
    done = asyncio.Event()
    last_progress = 0
    err = []

    def on_notify(_h, data: bytearray):
        nonlocal last_progress
        if not data: return
        op = chr(data[0])
        if op == 'O':
            done.set()
        elif op == 'P' and len(data) >= 5:
            last_progress = struct.unpack('<I', data[1:5])[0]
        elif op == 'E':
            err.append(bytes(data[1:]).decode(errors='replace'))
            done.set()

    async with BleakClient(addr, timeout=20) as cli:
        print(f"connected, mtu={cli.mtu_size}")
        await cli.start_notify(CTL, on_notify)

        # START
        done.clear()
        await cli.write_gatt_char(CTL, b'S' + struct.pack('<I', len(fw)), response=True)
        await asyncio.wait_for(done.wait(), timeout=10)
        if err: sys.exit(f"start failed: {err[0]}")
        print("transferring...")

        t0 = time.time()
        sent = 0
        for i in range(0, len(fw), CHUNK):
            chunk = fw[i:i + CHUNK]
            await cli.write_gatt_char(DATA, chunk, response=False)
            sent += len(chunk)
            if (sent % (16 * 1024)) < CHUNK or sent == len(fw):
                pct = 100 * sent / len(fw)
                kbps = sent / 1024 / max(time.time() - t0, 0.001)
                print(f"\r{sent}/{len(fw)} ({pct:.1f}%)  {kbps:.1f} KB/s   "
                      f"acked={last_progress}", end="", flush=True)
        print()

        # END — closes file on the watch
        done.clear()
        await cli.write_gatt_char(CTL, b'E', response=True)
        await asyncio.wait_for(done.wait(), timeout=15)
        if err: sys.exit(f"end failed: {err[0]}")
        print("OK — firmware.bin on SD. Tap USB INSTALL on the watch to flash.")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fw", nargs="?", default=None,
                    help="firmware.bin path (default: .pio/build/watch/firmware.bin)")
    ap.add_argument("--name", default="smartwatch", help="BLE device name")
    ap.add_argument("--addr", default=None, help="skip scan, use MAC directly")
    ap.add_argument("--scan-timeout", type=float, default=10.0)
    args = ap.parse_args()

    fw_path = Path(args.fw) if args.fw else (
        Path(__file__).resolve().parent.parent / ".pio" / "build" / "watch" / "firmware.bin")
    if not fw_path.exists():
        sys.exit(f"firmware not found: {fw_path}")
    fw = fw_path.read_bytes()
    print(f"firmware: {fw_path}  ({len(fw)} bytes)")

    addr = args.addr or asyncio.run(find(args.name, args.scan_timeout))
    asyncio.run(flash(addr, fw))

if __name__ == "__main__":
    main()
