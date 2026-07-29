#!/usr/bin/env python3
"""Fix the Genesis ROM header checksum (word sum of ROM after 0x200)."""
import struct
import sys

path = sys.argv[1]
with open(path, "rb") as f:
    data = bytearray(f.read())

s = 0
for i in range(0x200, len(data) - 1, 2):
    s = (s + struct.unpack_from(">H", data, i)[0]) & 0xFFFF

struct.pack_into(">H", data, 0x18E, s)
# also patch ROM end address in the header
struct.pack_into(">I", data, 0x1A4, len(data) - 1)

with open(path, "wb") as f:
    f.write(data)
print(f"checksum 0x{s:04X}, size {len(data)} bytes")
