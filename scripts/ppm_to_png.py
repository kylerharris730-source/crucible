"""PPM -> PNG, with nothing but the standard library.

tools/cover.cpp writes PPM because a PPM is a short header followed by raw
bytes, which needs no image library on the C++ side. PNG needs zlib and a
handful of chunks, which Python already has -- so the conversion lives here
rather than pulling an encoder into the game's build.

    python scripts/ppm_to_png.py build/cover-night-lava.ppm web/og-cover.png
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()

    # P6, then width height maxval, any of which may be separated by comments.
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # the single whitespace byte before the pixel data

    width, height, maxval = fields
    if maxval != 255:
        raise ValueError("only 8-bit PPM is supported, got maxval=%d" % maxval)
    expected = width * height * 3
    pixels = data[pos:pos + expected]
    if len(pixels) != expected:
        raise ValueError("truncated: wanted %d bytes of pixels, found %d"
                         % (expected, len(pixels)))
    return width, height, pixels


def write_png(path, width, height, pixels):
    # Filter type 0 (None) at the start of every scanline: the image is flat
    # colour blocks, so the fancier filters buy little and cost clarity here.
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)
    return len(png)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    width, height, pixels = read_ppm(src)
    size = write_png(dst, width, height, pixels)
    print("%s -> %s  (%dx%d, %d KB)" % (src, dst, width, height, size // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
