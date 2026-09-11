"""Label the spear_art harness's actual raster output at default 2x game scale."""
import sys
from PIL import Image, ImageDraw
im=Image.open(sys.argv[1]).resize((768,1400),Image.Resampling.NEAREST)
draw=ImageDraw.Draw(im)
for i,name in enumerate(["Copper","Bronze","Iron","Gold","Steel","Titanium","Tungsten"]):
    draw.text((16,i*200+10),name,fill="#e3dcca")
    draw.text((16,i*200+165),"Held / extended thrust / upward aim / facing left",fill="#a7b2c4")
im.save(sys.argv[2])
