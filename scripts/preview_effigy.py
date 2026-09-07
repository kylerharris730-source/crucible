"""Render the Effigy art harness dump as a contact sheet and animated preview."""
import sys
from PIL import Image, ImageDraw
rows=[line.split() for line in open(sys.argv[1])]
sheet=Image.new("RGB",(8*202,3*248),"#20242d")
draw=ImageDraw.Draw(sheet)
clips={"Walk":[],"Ritual":[]}
for i,row in enumerate(rows):
    name,f,w,h=row[:4]
    w,h=int(w),int(h)
    icon=Image.new("RGBA",(w,h))
    icon.putdata([((int(v,16)>>16)&255,(int(v,16)>>8)&255,int(v,16)&255,255 if int(v,16) else 0) for v in row[4:]])
    big=icon.resize((w*2,h*2),Image.Resampling.NEAREST)
    x,y=i%8*202,i//8*248
    sheet.paste(big,(x,y+20),big)
    draw.text((x+5,y+3),name+" "+f,fill="#e3d8be")
    frame=Image.new("RGB",(240,260),"#20242d")
    frame.paste(big,(24,30),big)
    clips[name].append(frame)
sheet.save(sys.argv[2]+".png")
for name,frames in clips.items():
    frames[0].save(sys.argv[2]+"-"+name.lower()+".gif",save_all=True,append_images=frames[1:],duration=350 if name=="Walk" else 175,loop=0)
