import hashlib
import struct
import time
import unittest
import zlib

from host import fw_tool as fw


def payload(slot):
    return struct.pack("<II", 0x20020000, fw.VECTOR[slot] + 513) + bytes(520)


class SerialBuffer:
    def __init__(self, data):
        self.data = bytearray(data)

    def read(self, size):
        result = bytes(self.data[:size])
        del self.data[:size]
        return result


class PackageTests(unittest.TestCase):
    def test_wire_header(self):
        application = payload("a")
        image = fw.package(application, "a", 7)
        fields = struct.unpack_from("<IHHIIII32sI", image)
        self.assertEqual(fields[:7], (0x46574D47, 1, 512, 7, 0x767, len(application), 512))
        self.assertEqual(fields[7], hashlib.sha256(application).digest())
        self.assertEqual(fields[8], zlib.crc32(image[:56]))
        self.assertEqual(image[512:], application)
        fw.check_package(image, "a")

    def test_wrong_slot(self):
        with self.assertRaises(ValueError):
            fw.package(payload("a"), "b", 1)

    def test_corrupt_body(self):
        image = fw.package(payload("b"), "b", 1)
        with self.assertRaises(ValueError):
            fw.check_package(image[:-1] + b"x", "b")

    def test_rejects_short_payload(self):
        with self.assertRaises(ValueError):
            fw.package(bytes(511), "a", 1)


class ProtocolTests(unittest.TestCase):
    def test_parser_resynchronizes_after_bad_crc(self):
        good = fw.frame(0x81, 255, b"\x00\x01")
        corrupted = good[:-1] + bytes([good[-1] ^ 1])
        serial = SerialBuffer(b"noise" + corrupted + good)
        self.assertEqual(fw.receive(serial, time.monotonic() + 0.25), (0x81, 255, b"\x00\x01"))

    def test_oversized_frame(self):
        with self.assertRaises(ValueError):
            fw.frame(1, 0, bytes(513))

    def test_lost_ack_reuses_identical_frame(self):
        class RetryPort(SerialBuffer):
            def __init__(self):
                super().__init__(b"")
                self.writes = []

            def write(self, packet):
                self.writes.append(packet)
                if len(self.writes) == 2:
                    self.data.extend(fw.frame(fw.WRITE | 0x80, 9, b"\x00"))

        device = object.__new__(fw.Device)
        device.port = RetryPort()
        device.sequence = 9
        device.call(fw.WRITE, b"chunk", timeout=0.01)
        self.assertEqual(device.port.writes[0], device.port.writes[1])
        self.assertEqual(device.sequence, 10)


if __name__ == "__main__":
    unittest.main()
