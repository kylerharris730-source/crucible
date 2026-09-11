"""Render widow_art's actual entity-renderer output at 3x for visual review."""
import sys
from PIL import Image, ImageDraw
sheet=Image.new("RGB",(840,172),"#20242d")
draw=ImageDraw.Draw(sheet)
for i,line in enumerate(open(sys.argv[1])):
    name,values=line.split("\t")
    icon=Image.new("RGBA",(56,48))
    icon.putdata([((int(v,16)>>16)&255,(int(v,16)>>8)&255,int(v,16)&255,255 if int(v,16) else 0) for v in values.split()])
    icon=icon.resize((168,144),Image.Resampling.NEAREST)
    sheet.paste(icon,(i*168,24),icon)
    draw.text((i*168+12,8),name,fill="#ded8eb")
sheet.save(sys.argv[2])
