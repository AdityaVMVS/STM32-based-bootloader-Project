"""Package slot-linked firmware and send it over the Nucleo ST-LINK VCP."""
import argparse
import hashlib
import secrets
import struct
import time
import zlib
from pathlib import Path

SOF = b"\xa5\x5a"
FORMAT = 1
HARDWARE = 0x767
HEADER_BYTES = 512
SLOT_BYTES = 768 * 1024
VECTOR = {"a": 0x08040200, "b": 0x08100200}
HELLO, GET_INFO, BEGIN, ERASE, WRITE, PROGRESS, VERIFY, ACTIVATE, REBOOT, ABORT, LOG = range(1, 12)
STATUS = ["OK", "bad argument", "bad state", "flash error", "invalid image", "bad sequence"]


def check_payload(payload, slot):
    if not 512 <= len(payload) <= SLOT_BYTES - HEADER_BYTES:
        raise ValueError("payload length is outside the slot limits")
    stack, entry = struct.unpack_from("<II", payload)
    start = VECTOR[slot]
    if not (0x20000000 < stack <= 0x20080000 and stack % 8 == 0
            and entry & 1 and start + 512 <= entry & ~1 < start + len(payload)):
        raise ValueError(f"invalid vectors or payload is linked for a slot other than {slot.upper()}")


def package(payload, slot, version):
    check_payload(payload, slot)
    if not 0 <= version <= 0xffffffff:
        raise ValueError("version must fit in an unsigned 32-bit integer")
    fields = struct.pack("<IHHIIII32s", 0x46574D47, FORMAT, HEADER_BYTES, version,
                         HARDWARE, len(payload), HEADER_BYTES, hashlib.sha256(payload).digest())
    header = (fields + struct.pack("<I", zlib.crc32(fields))).ljust(HEADER_BYTES, b"\xff")
    return header + payload


def check_package(image, slot):
    if len(image) < HEADER_BYTES:
        raise ValueError("truncated image header")
    version = struct.unpack_from("<I", image, 8)[0]
    if package(image[HEADER_BYTES:], slot, version) != image:
        raise ValueError("image header or checksum is invalid")


def frame(command, sequence, payload=b""):
    if len(payload) > 512:
        raise ValueError("packet payload exceeds 512 bytes")
    body = struct.pack("<BBHH", FORMAT, command, sequence, len(payload)) + payload
    return SOF + body + struct.pack("<I", zlib.crc32(body))


def receive(port, deadline):
    pending = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        pending.extend(byte)
        while True:
            start = pending.find(SOF)
            if start < 0:
                pending[:] = pending[-1:] if pending[-1:] == SOF[:1] else b""
                break
            del pending[:start]
            if len(pending) < 8:
                break
            version, command, sequence, length = struct.unpack_from("<BBHH", pending, 2)
            if version != FORMAT or length > 512:
                del pending[0]
                continue
            if len(pending) < 12 + length:
                break
            checksum = struct.unpack_from("<I", pending, 8 + length)[0]
            if zlib.crc32(pending[2:8 + length]) == checksum:
                return command, sequence, bytes(pending[8:8 + length])
            del pending[0]
    return None


class Device:
    def __init__(self, port):
        self.port = port
        self.sequence = secrets.randbits(16)
        if self.call(HELLO) != bytes([FORMAT]):
            raise ValueError("device reported an unsupported protocol version")

    def call(self, command, payload=b"", timeout=1, retries=3):
        packet = frame(command, self.sequence, payload)
        for _ in range(retries + 1):
            self.port.write(packet)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                answer = receive(self.port, deadline)
                if answer is None:
                    break
                kind, sequence, data = answer
                if (kind, sequence) != (command | 0x80, self.sequence) or not data:
                    continue
                self.sequence = (self.sequence + 1) & 0xffff
                if data[0]:
                    status = STATUS[data[0]] if data[0] < len(STATUS) else str(data[0])
                    raise ValueError(f"command {command}: {status}")
                return data[1:]
        raise TimeoutError(f"command {command}: no reply")

    def info(self):
        confirmed, pending, single_bank, hardware = struct.unpack("<BBBI", self.call(GET_INFO))
        slots = {0: "A", 1: "B", 255: "none"}
        return slots[confirmed], slots[pending], bool(single_bank), hardware

    def update(self, slot, image):
        confirmed, pending, single_bank, hardware = self.info()
        if not single_bank or hardware != HARDWARE or confirmed == slot.upper() or pending != "none":
            raise ValueError("incompatible flash layout, active destination, or pending trial")
        self.call(BEGIN, bytes([slot == "b"]) + struct.pack("<I", len(image)))
        self.call(ERASE, timeout=30)
        for offset in range(0, len(image), 256):
            chunk = image[offset:offset + 256]
            self.call(WRITE, struct.pack("<I", offset) + chunk)
        self.call(VERIFY, timeout=20)
        self.call(ACTIVATE)
        self.call(REBOOT, retries=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    pack = actions.add_parser("package", help="add an image header to a raw application binary")
    pack.add_argument("--slot", choices=VECTOR, required=True)
    pack.add_argument("--version", type=int, required=True)
    pack.add_argument("input", type=Path)
    pack.add_argument("output", type=Path)
    for name in ("info", "update", "log", "abort", "reboot"):
        action = actions.add_parser(name)
        action.add_argument("--port", required=True)
        if name == "update":
            action.add_argument("--slot", choices=VECTOR, required=True)
            action.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "package":
            args.output.write_bytes(package(args.input.read_bytes(), args.slot, args.version))
            return
        if args.action == "update":
            image = args.image.read_bytes()
            check_package(image, args.slot)
        try:
            import serial
        except ImportError as error:
            raise ValueError("install host/requirements.txt to use a serial port") from error
        with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=2) as port:
            device = Device(port)
            if args.action == "info":
                confirmed, pending, single_bank, hardware = device.info()
                print(f"Confirmed: {confirmed} | Pending: {pending} | Single bank: {single_bank} | Target: {hardware:#x}")
            elif args.action == "update":
                device.update(args.slot, image)
                print(f"Slot {args.slot.upper()} verified and marked pending. Trial confirmation runs on the board.")
            elif args.action == "log":
                attempts, generation = struct.unpack("<BI", device.call(LOG))
                print(f"Trial attempts: {attempts} | Journal generation: {generation}")
            else:
                device.call({"abort": ABORT, "reboot": REBOOT}[args.action], retries=0)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
