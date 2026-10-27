#!/usr/bin/env python3
"""Rebuild the fork's geometric artwork using only Python's standard library.

Run from any directory: python tools/generate-placeholder.py
No upstream artwork is read. Outputs are covered by the root MIT LICENSE.
"""

from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
EDGE = (244, 244, 244, 255)
DARK = (48, 48, 48, 255)
LIGHT = (224, 224, 224, 255)


def pixel(x, y, size, disabled=False):
    """A light square border, dark fill and two light circles; no antialiasing."""
    if x < size // 8 or x >= size - size // 8 or y < size // 8 or y >= size - size // 8:
        return EDGE
    circle = any((16 * x + 8 - cx * size) ** 2 +
                 (16 * y + 8 - 8 * size) ** 2 <= (2 * size) ** 2
                 for cx in (5, 11))
    color = LIGHT if circle else DARK
    if disabled:
        color = DARK if circle else LIGHT
    return color


def png(size, disabled=False):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', zlib.crc32(kind + data)))

    rows = b''.join(b'\0' + bytes(c for x in range(size)
                                for c in pixel(x, y, size, disabled))
                    for y in range(size))
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def dib(size, disabled=False):
    pixels = bytearray()
    for y in reversed(range(size)):
        for x in range(size):
            r, g, b, a = pixel(x, y, size, disabled)
            pixels.extend((b, g, r, a))
    mask = bytes(((size + 31) // 32) * 4 * size)
    return (struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0,
                        len(pixels) + len(mask), 0, 0, 0, 0) + bytes(pixels) + mask)


def ico(disabled=False):
    frames = [dib(size, disabled) if size <= 64 else png(size, disabled) for size in SIZES]
    offset = 6 + 16 * len(frames)
    entries = []
    for size, frame in zip(SIZES, frames):
        entries.append(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0,
                                   1, 32, len(frame), offset))
        offset += len(frame)
    return struct.pack('<HHH', 0, 1, len(frames)) + b''.join(entries) + b''.join(frames)


def about_bitmap():
    # Match the old bitmap dimensions so the existing About layout is untouched.
    width, height, size = 260, 61, 56
    left, top = (width - size) // 2, (height - size) // 2
    rows = []
    for y in reversed(range(height)):
        row = bytearray()
        for x in range(width):
            color = (240, 240, 240, 255)
            if left <= x < left + size and top <= y < top + size:
                color = pixel(x - left, y - top, size)
            row.extend((color[2], color[1], color[0]))
        row.extend(bytes((-len(row)) % 4))
        rows.append(row)
    data = b''.join(rows)
    return (struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54) +
            struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(data), 0, 0, 0, 0) + data)


def main():
    outputs = {
        'res/placeholder.ico': ico(),
        'res/placeholder-disabled.ico': ico(True),
        'res/about-placeholder.bmp': about_bitmap(),
        'docs/assets/placeholder.svg': (
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">\n'
            '  <rect width="16" height="16" fill="#f4f4f4"/>\n'
            '  <rect x="2" y="2" width="12" height="12" fill="#303030"/>\n'
            '  <g fill="#e0e0e0"><circle cx="5" cy="8" r="2"/>'
            '<circle cx="11" cy="8" r="2"/></g>\n'
            '</svg>\n').encode('ascii'),
    }
    for name, data in outputs.items():
        (ROOT / name).write_bytes(data)
        print(f'{name}: {len(data)} bytes')


if __name__ == '__main__':
    main()
