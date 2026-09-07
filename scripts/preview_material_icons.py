"""Render the material_icons test's pixel dump at actual 34px UI size and 2x."""
import sys
from PIL import Image, ImageDraw

rows = [line.strip().split("\t") for line in open(sys.argv[1])]
sheet = Image.new("RGB", (960, ((len(rows)+7)//8)*112), "#20242d")
draw = ImageDraw.Draw(sheet)
for i, row in enumerate(rows):
    name, values = row[0], row[1].split()
    icon = Image.new("RGBA", (21,21))
    icon.putdata([((int(v,16)>>16)&255, (int(v,16)>>8)&255, int(v,16)&255,
                  255 if int(v,16) else 0) for v in values])
    x, y = (i%8)*120, (i//8)*112
    small = icon.resize((34,34), Image.Resampling.NEAREST)
    large = icon.resize((63,63), Image.Resampling.NEAREST)
    sheet.paste(small,(x+3,y+23),small)
    sheet.paste(large,(x+48,y+8),large)
    draw.text((x+4,y+78),name,fill="#d8dee9")
sheet.save(sys.argv[2])
