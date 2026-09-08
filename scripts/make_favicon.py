"""Cinderlift's site icon: a rising cinder, drawn as pixel art.

Pixel art rather than a smooth vector mark, for two reasons. The game is pixel
art, so the icon should be; and a favicon is USED at 16 pixels, so authoring it
at 16 means what ships is what was drawn rather than whatever a downscaler made
of a larger picture.

Colours are the site's own (see :root in index.html), so the tab, the search
result and the page all agree.
"""
import io
import os
import sys

from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Kyler\Desktop\crucible\web"

BG    = (0x0d, 0x10, 0x14)     # --bg
EDGE  = (0xc2, 0x55, 0x1e)     # the cool outside of the flame
BODY  = (0xe0, 0x8a, 0x5a)     # --alarm
CORE  = (0xf5, 0xe0, 0x96)     # --accent, the hot middle

N = 16
CX = 7.5

# Half-width of the flame at each row: a point at the top, widest about two
# thirds down, and a rounded base. Rows outside this are background.
# Half-width of the flame at each row. A teardrop: a point at the top that
# widens STEADILY, widest about two thirds down, then a rounded base.
#
# The first attempt held a narrow stalk for six rows before flaring, and at any
# size it read as a bottle -- long neck, fat body, flat bottom. What makes a
# flame a flame is that it never stops widening on the way down.
WIDTH = {
    1: 0.6, 2: 1.1, 3: 1.7, 4: 2.3, 5: 2.9, 6: 3.5, 7: 4.1,
    8: 4.6, 9: 5.0, 10: 5.2, 11: 5.1, 12: 4.6, 13: 3.6, 14: 2.2,
}

filled = set()
for y, half in WIDTH.items():
    for x in range(N):
        if abs(x - CX) <= half:
            filled.add((x, y))

spark = set()


def tone(x, y):
    if (x, y) in spark:
        return CORE
    # Anything with a background neighbour is the outside of the flame.
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if (x + dx, y + dy) not in filled:
            return EDGE
    # The hot middle: an inner flame of the same shape, about half the width
    # and sitting lower, so the brightness follows the silhouette instead of
    # being a rectangle stamped into the middle of it.
    inner = WIDTH.get(y, 0.0) * 0.5
    if y >= 5 and abs(x - CX) <= inner:
        return CORE
    return BODY


art = Image.new("RGB", (N, N), BG)
px = art.load()
for (x, y) in filled:
    px[x, y] = tone(x, y)

os.makedirs(OUT, exist_ok=True)

# 96 is a multiple of 48, which is the size Google asks for. Nearest-neighbour
# so the pixels stay pixels at every scale.
for size in (16, 32, 48, 96, 192):
    art.resize((size, size), Image.NEAREST).save(
        os.path.join(OUT, "icon-%d.png" % size))

# One .ico as well, holding the small sizes. Browsers and crawlers ask for
# /favicon.ico whether or not anything links to it, and without one every
# visit and every crawl takes a 404.
art.resize((32, 32), Image.NEAREST).save(
    os.path.join(OUT, "favicon.ico"),
    sizes=[(16, 16), (32, 32), (48, 48)])

print("wrote icon-{16,32,48,96,192}.png and favicon.ico to " + OUT)
