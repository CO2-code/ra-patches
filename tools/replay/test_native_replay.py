import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from native_replay import load


def deflate(data):
    encoder = zlib.compressobj(wbits=-15)
    return encoder.compress(data) + encoder.flush()


def put32(data, offset, value):
    struct.pack_into("<I", data, offset, value)



class NativeReplayFormatTests(unittest.TestCase):
    def test_v2_without_chat_remains_readable(self):
        self.assertEqual(self.read(2)["chat"], [])

    def test_v3_restores_frame_ordered_chat(self):
        value = self.read(3, struct.pack("<IBBH", 17, 3, 4, 5) + b"Namehello")
        self.assertEqual(value["chat"], [dict(frame=17, color=3, nickname="Name", text="hello")])

    def test_v3_rejects_truncated_chat_record(self):
        with self.assertRaises(ValueError):
            self.read(3, b"\x00\x00")

    def read(self, version, chat=b""):
        header_size = 1312 if version == 2 else 1360
        header = bytearray(header_size)
        put32(header, 0, 0x50525259)
        put32(header, 4, 1)
        put32(header, 8, header_size)
        put32(header, 1052, 42)
        put32(header, 1056, 1)
        put32(header, 1096, 0x4E314152)
        put32(header, 1100, version)
        put32(header, 1104, 1)
        spawn = b"[Settings]\nScenario=spawnmap.ini\n"
        map_data = b"[Map]\n"
        raw_spawn = spawn
        events = b"event-data"
        checks = bytes(12)
        spawn_z, map_z, events_z, checks_z = map(deflate, (spawn, map_data, events, checks))
        raw_offset = header_size + len(spawn_z) + len(map_z)
        event_offset = raw_offset + len(raw_spawn)
        check_offset = event_offset + len(events_z)
        put32(header, 1032, len(spawn_z))
        put32(header, 1084, len(spawn))
        put32(header, 1036, len(map_z))
        put32(header, 1088, len(map_data))
        put32(header, 1108, raw_offset)
        put32(header, 1112, len(raw_spawn))
        put32(header, 1116, event_offset)
        put32(header, 1120, len(events_z))
        put32(header, 1124, len(events))
        put32(header, 1128, check_offset)
        put32(header, 1132, len(checks_z))
        put32(header, 1136, len(checks))
        header[1144:1176] = hashlib.sha256(events).digest()
        header[1176:1208] = hashlib.sha256(checks).digest()
        header[1240:1272] = hashlib.sha256(raw_spawn).digest()
        header[1272:1304] = hashlib.sha256(map_data).digest()
        chat_z = deflate(chat) if chat else b""
        if version == 3:
            put32(header, 1304, check_offset + len(checks_z))
            put32(header, 1308, len(chat_z))
            put32(header, 1312, len(chat))
            header[1316:1348] = hashlib.sha256(chat).digest()
        data = bytes(header) + spawn_z + map_z + raw_spawn + events_z + checks_z + chat_z
        with tempfile.NamedTemporaryFile(delete=False) as file:
            file.write(data)
            path = Path(file.name)
        try:
            return load(path)
        finally:
            path.unlink()


if __name__ == "__main__":
    unittest.main()
