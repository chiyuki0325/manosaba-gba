#!/usr/bin/env python3
"""Unlock the CG gallery and music room in a Manosaba Flash128K .sav.

Uses only Python's standard library. Writes a new file; never edits the input.
"""
import argparse
from pathlib import Path
import struct
import zlib

FLASH_SIZE = 128 * 1024
GLOBAL_PAGES = (120 * 1024, 124 * 1024)
PAGE_SIZE = 4096
PAYLOAD = 256
GLOBAL_SIZE = 120
GALLERY_OFFSET, GALLERY_COUNT = 92, 157
MUSIC_OFFSET, MUSIC_SIZE = 112, 8
HEADER = struct.Struct('<4sIIIHHHBBI')


def checksums(data, offset):
    length, seed = struct.unpack_from('<HH', data, offset + 18)
    crc = zlib.crc32(data[offset + 64:offset + PAYLOAD + length + seed])
    struct.pack_into('<I', data, offset + 12, crc)
    struct.pack_into('<I', data, offset + 24, zlib.crc32(data[offset:offset + 24]))


def unlock(source):
    if len(source) < FLASH_SIZE:
        raise ValueError('需要至少 128 KiB 的 Flash128K 存档，不能使用 64 KiB 存档或即时存档。')
    pages = []
    for offset in GLOBAL_PAGES:
        page = source[offset:offset + PAGE_SIZE]
        if page == b'\xff' * PAGE_SIZE or page[63] != 0xa5:
            continue
        magic, _, _, crc, version, length, seed, slot, _, header_crc = HEADER.unpack_from(page)
        if magic != b'MNSV' or slot != 14:
            raise ValueError('不是本游戏的全局存档记录。')
        if version != 3 or length != GLOBAL_SIZE or seed:
            raise ValueError('仅支持当前版本的全局存档格式（版本 3，120 字节）。')
        if header_crc != zlib.crc32(page[:24]) or crc != zlib.crc32(page[64:PAYLOAD + length]):
            raise ValueError('全局存档校验失败，未生成修改文件。')
        pages.append(offset)

    result = bytearray(source)
    if not pages:
        # A fresh save has not written either global journal page yet.
        if source[GLOBAL_PAGES[0]:FLASH_SIZE] != b'\xff' * (2 * PAGE_SIZE):
            raise ValueError('未找到已提交的全局记录，请先在游戏中存档或修改一次设置。')
        if source[:GLOBAL_PAGES[0]] != b'\xff' * GLOBAL_PAGES[0]:
            recognized = any(
                source[at:at + 4] == b'MNSV' and source[at + 63] == 0xa5
                and struct.unpack_from('<I', source, at + 24)[0] == zlib.crc32(source[at:at + 24])
                for at in range(0, GLOBAL_PAGES[0], 8192)
            )
            if not recognized:
                raise ValueError('未找到本游戏的存档记录。')
        offset = GLOBAL_PAGES[0]
        HEADER.pack_into(result, offset, b'MNSV', 1, 0, 0, 3, GLOBAL_SIZE, 0, 14, 0, 0)
        result[offset + 63] = 0xa5
        result[offset + 64:offset + PAYLOAD + GLOBAL_SIZE] = bytes(192 + GLOBAL_SIZE)
        struct.pack_into('<H', result, offset + 64 + 10, 65535)
        struct.pack_into('<H', result, offset + PAYLOAD, 1024)
        pages.append(offset)

    for offset in pages:
        base = offset + PAYLOAD
        for bit in range(GALLERY_COUNT):
            result[base + GALLERY_OFFSET + bit // 8] |= 1 << (bit % 8)
        result[base + MUSIC_OFFSET:base + MUSIC_OFFSET + MUSIC_SIZE] = b'\xff' * MUSIC_SIZE
        checksums(result, offset)
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description='解锁本游戏存档中的全部 CG 和音乐，保留剧情、设置及 RTC 附加数据。')
    parser.add_argument('save', type=Path, help='输入 .sav 文件')
    parser.add_argument('-o', '--output', type=Path, help='输出路径，默认：原名.unlocked.sav')
    args = parser.parse_args()
    output = args.output or args.save.with_suffix('.unlocked.sav')
    try:
        if output.resolve() == args.save.resolve():
            raise ValueError('输出路径不能与输入存档相同。')
        patched = unlock(args.save.read_bytes())
        with output.open('xb') as stream:
            stream.write(patched)
    except FileExistsError:
        parser.error('输出文件已存在，请使用 -o 指定另一个文件名。')
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'已解锁全部 {GALLERY_COUNT} 张 CG 和音乐馆曲目：{output}')
    print('原存档未修改。请关闭模拟器，将输出文件改为游戏使用的存档文件名。')


if __name__ == '__main__':
    main()
