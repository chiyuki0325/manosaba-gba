import sys
from pathlib import Path
p = Path(sys.argv[1])
b = bytearray(p.read_bytes())
logo = bytes.fromhex('24 ff ae 51 69 9a a2 21 3d 84 82 0a 84 e4 09 ad 11 24 8b 98 c0 81 7f 21 a3 52 be 19 93 09 ce 20 10 46 4a 4a f8 27 31 ec 58 c7 e8 33 82 e3 ce bf 85 f4 df 94 ce 4b 09 c1 94 56 8a c0 13 72 a7 fc 9f 84 4d 73 a3 ca 9a 61 58 97 a3 27 fc 03 98 76 23 1d c7 61 03 04 ae 56 bf 38 84 00 40 a7 0e fd ff 52 fe 03 6f 95 30 f1 97 fb c0 85 60 d6 80 25 a9 63 be 03 01 4e 38 e2 f9 a2 34 ff bb 3e 03 44 78 00 90 cb 88 11 3a 94 65 c0 7c 63 87 f0 3c af d6 25 e4 8b 38 0a ac 72 21 d4 f8 07')
b[4:0xa0] = logo
b[0xa0:0xac] = b'MANOSABA DEM'
b[0xac:0xb0] = b'MSBE'
b[0xb0:0xb2] = b'00'
b[0xb2] = 0x96
b[0xbd] = (-sum(b[0xa0:0xbd]) - 0x19) & 255
p.write_bytes(b)
