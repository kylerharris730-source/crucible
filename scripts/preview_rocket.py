"""Render the rocket art harness dump as the three states it has in the world.

    build/tbin/rocket_art.exe dump.txt
    python scripts/preview_rocket.py dump.txt preview.png

Left to right: empty, core installed, core and fuel aboard. The dimming here
mirrors devDraw -- an unfilled part keeps its shape and loses its light -- so
this is a preview of the object rather than of the art table.
"""
import sys
from PIL import Image, ImageDraw

lines = open(sys.argv[1]).read().split("\n")
W, H = (int(v) for v in lines[0].split())
cells = [tok.split(":") for line in lines[1:] if line.strip() for tok in line.split()]

SCALE = 5
sheet = Image.new("RGB", ((W * SCALE + 30) * 3 + 30, H * SCALE + 60), "#20242d")
draw = ImageDraw.Draw(sheet)

for panel, (core, fuel) in enumerate([(False, False), (True, False), (True, True)]):
    img = Image.new("RGBA", (W, H))
    px = []
    for colour, part in cells:
        v, part = int(colour, 16), int(part)
        if not v:
            px.append((0, 0, 0, 0))
            continue
        lit = (part == 0) or (part == 1 and core) or (part == 2 and fuel)
        r, g, b = (v >> 16) & 255, (v >> 8) & 255, v & 255
        if not lit:
            r, g, b = r // 5, g // 5, b // 4 + 24
        px.append((r, g, b, 255))
    img.putdata(px)
    big = img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST)
    x = 30 + panel * (W * SCALE + 30)
    sheet.paste(big, (x, 40), big)
    draw.text((x, 20), ["empty", "core installed", "fuelled"][panel], fill="#e3d8be")

sheet.save(sys.argv[2])
print("wrote", sys.argv[2])
