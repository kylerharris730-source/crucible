#include "spear_art.h"
#include <math.h>
#include <algorithm>

namespace {
struct Design { u32 metal,edge,shade,wood,wrap; float head,width,shoulder; bool wings; };
Design design(ItemId id) {
    switch (id) {
    case ITEM_SPEAR_COPPER: return {0xD77D58,0xF7B994,0x82462F,0x765038,0xB49A72,11,2.2f,0.36f,false};
    case ITEM_SPEAR_BRONZE: return {0xAD893E,0xE5CC7C,0x695323,0x694831,0xAC8153,12,2.7f,0.25f,true};
    case ITEM_SPEAR_IRON: return {0xA8ADB6,0xE0E5E9,0x59616E,0x735B45,0xB5AA90,12,2.5f,0.43f,false};
    case ITEM_SPEAR_GOLD: return {0xE8C233,0xFFF0A2,0x967022,0x514037,0xB59056,13,2.8f,0.36f,true};
    case ITEM_SPEAR_STEEL: return {0x8E97A6,0xD6E5EE,0x465465,0x4D4540,0x9B9DA2,15,2.1f,0.23f,false};
    case ITEM_SPEAR_TITANIUM: return {0xD2DAE4,0xF1FCFF,0x8299AE,0x4B5967,0xA0C7CC,16,2.4f,0.32f,false};
    case ITEM_SPEAR_TUNGSTEN: return {0x6F7A86,0xAFBDCC,0x3F4956,0x51483E,0x9E8D72,14,3.1f,0.20f,false};
    default: return {0xA8ADB6,0xE0E5E9,0x59616E,0x735B45,0xB5AA90,12,2.5f,0.43f,false};
    }
}
u32 sample(const Design& d,float t,float side,float length) {
    if (t<0 || t>length) return 0;
    const float head=std::min(d.head,length*0.50f), start=length-head;
    const float across=fabsf(side);
    if (t>=start) {
        const float f=(t-start)/head;
        // Leaf/diamond blade: narrow socket, broad shoulders, a real point.
        const float radius=f<d.shoulder ? 0.65f+(d.width-0.65f)*f/d.shoulder
                                         : d.width*(1-f)/(1-d.shoulder);
        if (across>std::max(0.28f,radius)) return 0;
        if (across<0.40f || side<-radius+0.65f) return d.edge;
        return side<0 ? d.metal : d.shade;
    }
    if (t>=start-3) {
        float radius=0.95f;
        if (d.wings && t>start-1.5f) radius=2.3f;
        if (across>radius) return 0;
        return side<0.1f ? d.metal : d.shade;
    }
    if (across>0.75f) return 0;
    if (t<1.5f) return d.shade; // Small ferrule, not a sword pommel.
    if (t<7.0f) return ((int)t%3==0) ? d.wood : d.wrap;
    return side<0.0f ? d.wrap : d.wood;
}
}

void drawHeldSpear(u32* pixels,int width,int height,ItemId item,
                   float x0,float y0,float x1,float y1,u32 (*lighting)(u32,int,int)) {
    const float dx=x1-x0,dy=y1-y0,length=sqrtf(dx*dx+dy*dy);
    if (length<0.5f) return;
    const Design d=design(item);
    const float ux=dx/length,uy=dy/length;
    const int left=std::max(0,(int)floorf(std::min(x0,x1)-4));
    const int right=std::min(width-1,(int)ceilf(std::max(x0,x1)+4));
    const int top=std::max(0,(int)floorf(std::min(y0,y1)-4));
    const int bottom=std::min(height-1,(int)ceilf(std::max(y0,y1)+4));
    // Inverse sampling gives a continuous silhouette at diagonal aim angles.
    for (int y=top;y<=bottom;++y) for (int x=left;x<=right;++x) {
        const float rx=x-x0,ry=y-y0;
        u32 c=sample(d,rx*ux+ry*uy,-rx*uy+ry*ux,length);
        if (c) pixels[y*width+x]=lighting ? lighting(c,x,y) : c;
    }
}
