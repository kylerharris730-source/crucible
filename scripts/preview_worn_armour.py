"""Draw the worn_armour test dump as a pose-by-set contact sheet (Pillow)."""
import sys
from PIL import Image, ImageDraw
rows=[line.strip().split("\t") for line in open(sys.argv[1])]
names=list(dict.fromkeys(r[0] for r in rows))
sheet=Image.new("RGB",(1100,len(names)*136+28),"#20242d")
draw=ImageDraw.Draw(sheet)
for row in rows:
    name,frame,w,tail=row
    h,*values=tail.split()
    w,h,frame=int(w),int(h),int(frame)
    icon=Image.new("RGBA",(w,h))
    icon.putdata([((int(v,16)>>16)&255,(int(v,16)>>8)&255,int(v,16)&255,255 if int(v,16) else 0) for v in values])
    x,y=110+frame*76,28+names.index(name)*136
    large=icon.resize((w*3,h*3),Image.Resampling.NEAREST)
    sheet.paste(large,(x,y+118-h*3),large)
    if frame==0:
        draw.text((8,y+45),name,fill="#e3e5eb")
        small=icon.resize((w*2,h*2),Image.Resampling.NEAREST)
        sheet.paste(small,(68,y+118-h*2),small)
    if name==names[0]: draw.text((x,6),"Crouch" if frame==12 else str(frame),fill="#a9b6ca")
sheet.save(sys.argv[2])
