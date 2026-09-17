#!/usr/bin/env python3
"""Converts screen_preview's PBM files to PNG, enlarged for viewing.

Usage: tools/pbm_to_png.py [--scale N] file.pbm [...]

Standard library only. Pixels are drawn as e-paper colours on a grey frame.
"""
import argparse
import struct
import zlib

INK, PAPER, FRAME = 20, 235, 150


def read_pbm(path):
    with open(path, "rb") as f:
        magic, size, bits = f.read().split(b"\n", 2)
    if magic != b"P4":
        raise ValueError(f"{path}: not a binary PBM")
    width, height = map(int, size.split())
    stride = (width + 7) // 8
    return width, height, [
        [(bits[y * stride + x // 8] >> (7 - x % 8)) & 1 for x in range(width)] for y in range(height)
    ]


def write_png(path, width, height, pixels, scale, border):
    out_w, out_h = width * scale + 2 * border, height * scale + 2 * border
    rows = []
    for out_y in range(out_h):
        row = bytearray([0])  # PNG filter type: none
        for out_x in range(out_w):
            x, y = (out_x - border) // scale, (out_y - border) // scale
            if 0 <= out_x - border < width * scale and 0 <= out_y - border < height * scale:
                row.append(INK if pixels[y][x] else PAPER)
            else:
                row.append(FRAME)
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    header = struct.pack(">IIBBBBB", out_w, out_h, 8, 0, 0, 0, 0)  # 8-bit greyscale
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(b"".join(rows))))
        f.write(chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scale", type=int, default=2)
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()
    for path in args.files:
        width, height, pixels = read_pbm(path)
        out = path.rsplit(".", 1)[0] + ".png"
        write_png(out, width, height, pixels, args.scale, border=4 * args.scale)
        print(out)


if __name__ == "__main__":
    main()
