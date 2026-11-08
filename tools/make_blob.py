import struct, sys, zlib

src, dst = sys.argv[1], sys.argv[2]
version = int(sys.argv[3]) if len(sys.argv) > 3 else 1

data = open(src, "rb").read()
crc = zlib.crc32(data) & 0xFFFFFFFF
header = struct.pack("<4sIII", b"MDL1", version, len(data), crc) 
open(dst, "wb").write(header + data)
print(f"versao={version} tamanho={len(data)} crc32=0x{crc:08x}")