#!/usr/bin/env python3
"""Exercise the same Nordic UART GATT interface used by the phone remote.

Install `bleak` into the project venv first. Default behavior sends only `status`:
  .venv/bin/python firmware/tools/test_ble_remote.py --name Chiikawa-PRO
Add --smoke for reversible control checks. This never purchases furniture,
feeds the pet, skips a timer, or resets progress. --self-test needs no Bluetooth.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
ROOM_SLOTS = ("wallpaper", "rug", "bed", "decor")


class ProtocolError(RuntimeError):
    pass


class CommandRejected(ProtocolError):
    def __init__(self, command: str, response: dict):
        self.response = response
        super().__init__(f"{command!r} rejected: {response.get('error', response)}")


class JsonLineDecoder:
    """Keep bytes until newline so fragmented UTF-8 is decoded only when whole."""

    def __init__(self, limit: int = 4096):
        self.pending = bytearray()
        self.limit = limit

    def feed(self, data: bytes | bytearray) -> list[dict]:
        self.pending.extend(data)
        messages = []
        while b"\n" in self.pending:
            line, _, rest = self.pending.partition(b"\n")
            self.pending = bytearray(rest)
            if len(line) > self.limit:
                raise ProtocolError("BLE response exceeded framing limit")
            if not line.strip():
                continue
            try:
                message = json.loads(line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                raise ProtocolError(f"Invalid BLE JSON response: {error}") from error
            if not isinstance(message, dict):
                raise ProtocolError("BLE response must be a JSON object")
            messages.append(message)
        if len(self.pending) > self.limit:
            raise ProtocolError("BLE response has no newline within framing limit")
        return messages


class RemoteSession:
    def __init__(self, client, timeout: float = 12):
        self.client = client
        self.timeout = timeout
        self.decoder = JsonLineDecoder()
        self.inbox: asyncio.Queue = asyncio.Queue(maxsize=32)
        self.lock = asyncio.Lock()
        self.records: list[dict] = []

    def notification(self, _characteristic, data: bytearray) -> None:
        try:
            messages = self.decoder.feed(data)
        except ProtocolError as error:
            messages = [error]
        for message in messages:
            if self.inbox.full():
                self.inbox.get_nowait()
                message = ProtocolError("BLE response queue overflow")
            self.inbox.put_nowait(message)

    async def command(self, command: str, *, accepted: bool = True) -> dict:
        if not command or any(c in command for c in "\r\n"):
            raise ValueError("Send one non-empty command without newline characters")
        try:
            payload = (command + "\n").encode("ascii")
        except UnicodeEncodeError as error:
            raise ValueError("BLE commands must be ASCII") from error
        if len(payload) > 96:
            raise ValueError("BLE command exceeds95bytes before newline")
        async with self.lock:
            # Only one command is in flight; periodic status has no command echo.
            while not self.inbox.empty():
                self.inbox.get_nowait()
            for offset in range(0, len(payload), 20):
                await asyncio.wait_for(
                    self.client.write_gatt_char(RX_UUID, payload[offset:offset + 20], response=True),
                    self.timeout,
                )
                if offset + 20 < len(payload):
                    await asyncio.sleep(0.03)
            deadline = time.monotonic() + self.timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"No matching acknowledgment for {command!r}")
                try:
                    message = await asyncio.wait_for(self.inbox.get(), remaining)
                except TimeoutError as error:
                    raise TimeoutError(f"No matching acknowledgment for {command!r}") from error
                if isinstance(message, Exception):
                    raise message
                if message.get("command") != command:
                    continue
                self.records.append({"command": command, "response": message})
                if accepted and message.get("ok") is not True:
                    raise CommandRejected(command, message)
                if not accepted and message.get("ok") is not False:
                    raise ProtocolError(f"Expected rejection for {command!r}")
                return message


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProtocolError(message)


async def smoke(remote: RemoteSession) -> dict:
    initial = await remote.command("status")
    require(initial.get("screen") in ("clock", "pet", "exchange", "mario", "pomodoro"),
            "Status missing valid screen")
    require(initial.get("character") in ("chiikawa", "hachiware", "usagi", "trio"),
            "Status missing valid character")
    require(isinstance(initial.get("brightness"), int), "Status missing brightness")
    require(isinstance(initial.get("room"), dict), "Status missing room")
    timer_changed = False
    restore_errors = []
    checks = []
    try:
        rejected = await remote.command("unknown", accepted=False)
        require(bool(rejected.get("error")), "Invalid command rejection needs an error")
        checks.append("unknown command rejected")
        for mode in ("clock", "pet", "exchange", "mario", "pomodoro"):
            status = await remote.command(f"screen {mode}")
            require(status.get("screen") == mode, f"Screen did not change to {mode}")
        checks.append("five screens")
        other_character = "hachiware" if initial["character"] != "hachiware" else "chiikawa"
        status = await remote.command(f"char {other_character}")
        require(status.get("character") == other_character, "Character did not change")
        checks.append("character")
        other_brightness = initial["brightness"] - 1 if initial["brightness"] > 3 else 4
        status = await remote.command(f"bright {other_brightness}")
        require(status.get("brightness") == other_brightness, "Brightness did not change")
        checks.append("brightness")
        before_timer = (await remote.command("status")).get("pomodoro", {})
        if before_timer.get("remainingSeconds", 0) > 60:
            first = "pause" if before_timer.get("running") else "start"
            second = "start" if before_timer.get("running") else "pause"
            timer_changed = True
            status = await remote.command(f"pomo {first}")
            require(status["pomodoro"]["running"] == (first == "start"), "Timer first control failed")
            status = await remote.command(f"pomo {second}")
            require(status["pomodoro"]["running"] == (second == "start"), "Timer second control failed")
            require(status["pomodoro"]["completedFocus"] == before_timer["completedFocus"],
                    "Timer completion changed during reversible smoke test")
            checks.append("pomodoro start/pause")
        else:
            checks.append("pomodoro controls skipped: timer close to completion")
        room = initial["room"]
        equipped = [(slot, room.get(key, 0)) for slot, key in enumerate(ROOM_SLOTS) if room.get(key, 0)]
        if equipped:
            slot, item = equipped[0]
            cleared = await remote.command(f"room clear {slot}")
            require(cleared["room"][ROOM_SLOTS[slot]] == 0, "Furniture did not clear")
            restored = await remote.command(f"room equip {item}")
            require(restored["room"][ROOM_SLOTS[slot]] == item, "Furniture did not equip")
            require(restored["room"]["coins"] == room["coins"] and
                    restored["room"]["ownedMask"] == room["ownedMask"],
                    "Furniture re-equip changed coins or ownership")
            checks.append("owned furniture clear/equip without charge")
        else:
            checks.append("furniture controls skipped: nothing equipped")
    finally:
        async def restore(command: str) -> None:
            try:
                await remote.command(command)
            except Exception as error:
                restore_errors.append(f"{command}: {error}")
        # Best-effort restore every independent setting even after a failed check.
        for slot, key in enumerate(ROOM_SLOTS):
            item = initial["room"].get(key, 0)
            await restore(f"room equip {item}" if item else f"room clear {slot}")
        await restore(f"char {initial['character']}")
        await restore(f"bright {initial['brightness']}")
        await restore(f"screen {initial['screen']}")
        if timer_changed:
            try:
                current = await remote.command("status")
                if current["pomodoro"]["running"] != initial["pomodoro"]["running"]:
                    await restore("pomo start" if initial["pomodoro"]["running"] else "pomo pause")
            except Exception as error:
                restore_errors.append(f"timer state: {error}")
        if restore_errors:
            raise ProtocolError("Restore failed: " + "; ".join(restore_errors))
    final = await remote.command("status")
    for key in ("screen", "character", "brightness", "room"):
        require(final[key] == initial[key], f"Original {key} was not restored")
    require(final["pomodoro"]["running"] == initial["pomodoro"]["running"],
            "Original timer running state was not restored")
    for key in ("completedFocus", "totalFocusMinutes", "phase", "cycle"):
        require(final["pomodoro"][key] == initial["pomodoro"][key], f"Timer {key} changed")
    return {"result": "PASS", "checks": checks, "initial": initial, "final": final}


async def self_test() -> None:
    # Notification chunks may split UTF-8, JSON tokens, or multiple messages.
    encoded = ('{"ok":true,"error":"치이카와","command":"status"}\n'
               '{"ok":true,"screen":"pet"}\n').encode()
    decoder = JsonLineDecoder()
    messages = []
    for byte in encoded:
        messages.extend(decoder.feed(bytes((byte,))))
    require(len(messages) == 2 and messages[0]["error"] == "치이카와", "UTF8 framing failed")
    for malformed in (b"[]\n", b"{bad}\n", b"\xff\n", b"x" * 4097):
        try:
            JsonLineDecoder().feed(malformed)
        except ProtocolError:
            pass
        else:
            raise AssertionError("Malformed notification accepted")

    class FakeClient:
        def __init__(self):
            self.remote = None
            self.chunks = []
            self.buffer = bytearray()

        async def write_gatt_char(self, uuid, payload, *, response):
            require(uuid == RX_UUID and response and len(payload) <= 20, "Invalid GATT write")
            self.chunks.append(payload)
            self.buffer.extend(payload)
            if self.buffer.endswith(b"\n"):
                command = self.buffer[:-1].decode()
                self.buffer.clear()
                wire = (json.dumps({"ok": True, "screen": "clock"}) + "\n" +
                        json.dumps({"ok": True, "command": "different"}) + "\n" +
                        json.dumps({"ok": command != "unknown", "command": command,
                                    "error": "invalid" if command == "unknown" else ""}) + "\n").encode()
                for offset in range(0, len(wire), 7):
                    self.remote.notification(None, bytearray(wire[offset:offset + 7]))

    client = FakeClient()
    remote = RemoteSession(client, .15)
    client.remote = remote
    response = await remote.command("screen pomodoro" + " " * 22)
    require(response["command"].startswith("screen pomodoro"), "Command ack matching failed")
    require(len(client.chunks) == 2 and client.chunks[-1].endswith(b"\n"), "MTU chunking failed")
    await remote.command("unknown", accepted=False)
    try:
        await remote.command("unknown")
    except CommandRejected:
        pass
    else:
        raise AssertionError("Command rejection not raised")
    for bad in ("", "status\n", "status\r", "치이카와", "x" * 96):
        try:
            await remote.command(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid local command accepted")
    print("BLE test client framing,20byte writes,matching acknowledgments,rejections PASS")


async def run(args) -> dict:
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as error:
        raise RuntimeError("Install BLE client dependency: .venv/bin/python -m pip install bleak") from error
    print(f"Scanning for {args.name}...", file=sys.stderr, flush=True)
    device = await BleakScanner.find_device_by_name(
        args.name, timeout=args.scan_timeout, service_uuids=[SERVICE_UUID]
    )
    if device is None:
        raise RuntimeError(f"{args.name} not found within {args.scan_timeout:g}s")
    if args.scan_only:
        return {"result": "PASS", "scan": "found", "name": args.name}
    print(f"Connecting to {args.name}...", file=sys.stderr, flush=True)
    async with BleakClient(device, timeout=args.timeout) as client:
        service = client.services.get_service(SERVICE_UUID)
        require(service is not None, "Nordic UART service missing")
        rx = client.services.get_characteristic(RX_UUID)
        tx = client.services.get_characteristic(TX_UUID)
        require(rx is not None and tx is not None, "UART RX/TX characteristic missing")
        require("write" in rx.properties and "notify" in tx.properties, "UART properties missing")
        remote = RemoteSession(client, args.timeout)
        await client.start_notify(TX_UUID, remote.notification)
        try:
            if args.smoke:
                result = await smoke(remote)
            else:
                responses = [await remote.command(command) for command in (args.command or ["status"])]
                result = {"result": "PASS", "responses": responses}
            result["gatt"] = {"name": args.name, "service": SERVICE_UUID,
                              "mtu": client.mtu_size, "notify": True, "write": True}
            result["commands"] = remote.records
        finally:
            await client.stop_notify(TX_UUID)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", default="Chiikawa-PRO")
    parser.add_argument("--command", action="append", help="ASCII command; repeat for sequential commands; default: status")
    parser.add_argument("--smoke", action="store_true", help="Run reversible controls and restore original settings")
    parser.add_argument("--scan-only", action="store_true", help="Find device without connecting or sending commands")
    parser.add_argument("--timeout", type=float, default=12, help="GATT operation/ack timeout seconds")
    parser.add_argument("--scan-timeout", type=float, default=12)
    parser.add_argument("--output", type=Path, help="Optional local JSON validation record")
    parser.add_argument("--self-test", action="store_true", help="Test client framing without Bluetooth or bleak")
    args = parser.parse_args()
    if args.timeout <= 0 or args.scan_timeout <= 0:
        parser.error("Timeouts must be positive")
    if args.smoke and args.command:
        parser.error("Use --smoke or --command, not both")
    try:
        if args.self_test:
            asyncio.run(self_test())
            return 0
        result = asyncio.run(run(args))
        encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.write_text(encoded)
        print(encoded, end="")
        return 0
    except (Exception, KeyboardInterrupt) as error:
        print(f"BLE test failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
