#include "material_icon.h"
#include "materials.h"
#include <stdlib.h>
#include <string.h>

static u32 shade(u32 c, int delta) {
    u32 out = 0;
    for (int s = 0; s <= 16; s += 8) {
        int v = int((c >> s) & 255) + delta;
        out |= u32(v < 1 ? 1 : v > 255 ? 255 : v) << s;
    }
    return out;
}

void renderMaterialIcon(int m, u32* p) {
    memset(p, 0, INV_SPR_W * INV_SPR_H * sizeof(u32));
    if (m <= MAT_EMPTY || m >= MAT_COUNT) return;
    int sprite=SPR_NONE;
    switch (m) {
    case MAT_TORCH: sprite=SPR_TORCH; break;
    case MAT_STATION_BENCH: sprite=SPR_BENCH; break;
    case MAT_STATION_ANVIL: sprite=SPR_ANVIL; break;
    case MAT_STATION_CHEM: sprite=SPR_CHEMSTN; break;
    case MAT_STATION_ASSEMBLY: sprite=SPR_ASSEMBLY; break;
    case MAT_STATION_FORGE: sprite=SPR_FORGESTN; break;
    default: break;
    }
    if (sprite!=SPR_NONE) {
        for (int y=1; y<20; ++y) for (int x=1; x<20; ++x)
            p[y*INV_SPR_W+x]=g_sprite[sprite][(y*SPR_H/INV_SPR_H)*SPR_W+x*SPR_W/INV_SPR_W];
        return;
    }
    enum Form { ROCK, PILE, DROP, CLOUD, INGOT, ORE, LOG, PANE, BRICK,
                MESH, ROPE, DOOR, BOARD, LEAF, SEED, STALK, FLAME, SHELL, RING,
                WEB, SPRING, LAMP, COIL, COMB };
    Form form = ROCK;
    u32 base = MATS[m].dryA, vein = 0;
    if (MATS[m].kind == KIND_POWDER) form = PILE;
    if (MATS[m].kind == KIND_LIQUID) form = DROP;
    if (MATS[m].kind == KIND_GAS) form = CLOUD;
    switch (m) {
    case MAT_COPPER: form=INGOT; base=0xD77D58; break;
    case MAT_BRONZE: form=INGOT; base=0xAD893E; break;
    case MAT_IRON: form=INGOT; base=0xA6ADB8; break;
    case MAT_TIN: form=INGOT; base=0xC0D2CB; break;
    case MAT_STEEL: form=INGOT; base=0x6D8CA6; break;
    case MAT_GOLD: form=INGOT; base=0xE9B52F; break;
    case MAT_TITANIUM: form=INGOT; base=0xD5CDE6; break;
    case MAT_TUNGSTEN: form=INGOT; base=0x656774; break;
    case MAT_COPPER_ORE: form=ORE; vein=0xE58A61; break;
    case MAT_IRON_ORE: form=ORE; vein=0xB78465; break;
    case MAT_TIN_ORE: form=ORE; vein=0xCADDD4; break;
    case MAT_GOLD_ORE: form=ORE; vein=0xFFD34F; break;
    case MAT_TITANIUM_ORE: form=ORE; vein=0xC5B7E3; break;
    case MAT_TUNGSTEN_ORE: form=ORE; vein=0x9293AF; break;
    case MAT_WOOD: case MAT_BIRCH_WOOD: form=LOG; break;
    case MAT_GLASS: form=PANE; base=0x94CED9; break;
    case MAT_ICE: form=PANE; base=0x8DBCE8; break;
    case MAT_CERAMIC: form=BRICK; base=0xBA7455; break;
    case MAT_REFRACTORY: case MAT_WALL: case MAT_ALUMINUM_NITRIDE: form=BRICK; break;
    case MAT_SIEVE: case MAT_GAS_SIEVE: form=MESH; break;
    case MAT_ROPE: form=ROPE; base=0x886342; break;
    case MAT_DOOR: case MAT_DOOR_OPEN: form=DOOR; break;
    case MAT_PLATFORM: case MAT_GRAPHENE: form=BOARD; break;
    case MAT_OAK_LEAF: case MAT_BIRCH_LEAF: case MAT_GRASS: form=LEAF; break;
    case MAT_OAK_SEED: case MAT_BIRCH_SEED: case MAT_WHEAT_SEED: case MAT_FLOWER_SEED:
    case MAT_FLAX_SEED: case MAT_COTTON_SEED: case MAT_OAK_POD: case MAT_BIRCH_POD: form=SEED; break;
    case MAT_OAK_SAPLING: case MAT_BIRCH_SAPLING: case MAT_STALK:
    case MAT_STALK_DRY: case MAT_WHEAT: case MAT_FLAX: case MAT_COTTON: case MAT_FLOWER: form=STALK; break;
    case MAT_FIRE: case MAT_FUELFIRE: case MAT_PLASMA: case MAT_COLDFIRE: case MAT_BRIMFIRE: form=FLAME; break;
    case MAT_CHITIN: form=SHELL; base=0xBBA27A; break;
    case MAT_RUBBER: form=RING; base=0x454854; break;
    case MAT_COAL: form=ROCK; base=0x343642; break;
    case MAT_COKE: form=ROCK; base=0x637989; break;
    case MAT_COKE_EMBER: form=FLAME; break;
    case MAT_CINDERLING_EMBER: form=FLAME; break;
    case MAT_FUEL: form=PILE; base=0x475846; break;
    case MAT_CLAY: form=ROCK; base=0xAB806C; break;
    case MAT_WEB: form=WEB; break;
    case MAT_SPRING: form=SPRING; break;
    case MAT_LAMP: form=LAMP; break;
    case MAT_HEATER: case MAT_COOLER: form=COIL; break;
    case MAT_BEESWAX: form=COMB; break;
    default: break;
    }
    if (form == ORE) base=0x555560;
    for (int y=1; y<20; ++y) for (int x=1; x<20; ++x) {
        int dx=x-10, dy=y-11;
        bool on=false;
        u32 c=shade(base, x+y<19 ? 18 : -18);
        switch (form) {
        case INGOT:
            on=y>=6 && y<=16 && x>=2+abs(y-11)/3 && x<=18-abs(y-11)/3;
            c=shade(base,y<10 ? 53 : x>14 ? -55 : -8);
            if (y==10 || (y==6 && x>5 && x<15)) c=shade(base,82);
            break;
        case ROCK: case ORE:
            on=abs(dx)*2+abs(dy)*2<22 && y>=4 && y<=18;
            c=shade(base,x+y<19 ? 30 : x>11 ? -22 : 0);
            if (m==MAT_COKE && ((x*7+y*11)%19<3)) c=0x202C38; // porous carbon
            if (form==ORE && (abs(x-(7+y/4))<=1 || (y>=11 && abs(x+y-26)<2))) c=vein;
            if (form==ORE && m==MAT_COPPER_ORE && x<7 && y>9 && y<14) c=0x569B85;
            break;
        case PILE:
            on=y>=7+abs(dx)*3/4 && y<=18 && abs(dx)<9;
            c=shade(base, ((x*7+y*13)%17<3) ? 38 : (x+y)%5==0 ? -27 : 0);
            break;
        case DROP:
            on=(y<11 ? abs(dx)<=(y-2)/2 : dx*dx+(y-12)*(y-12)<49);
            c=shade(base,dx<-2 ? 35 : dx>3 ? -30 : 0);
            if (x==7 && y>=10 && y<=13) c=shade(base,95);
            break;
        case CLOUD:
            on=(dx*dx+dy*dy<42 || (x-6)*(x-6)+(y-8)*(y-8)<20 || (x-14)*(x-14)+(y-8)*(y-8)<18);
            c=shade(base,y<10 ? 42 : -28);
            if ((x==4 && y==17)||(x==17 && y==4)) on=true;
            break;
        case LOG:
            on=x>=5 && x<=16 && y>=3 && y<=18;
            c=m==MAT_BIRCH_WOOD ? 0xC9C5AF : 0x815132;
            if (y<6) c=(x>7 && x<14 && y==4) ? 0x785435 : 0xD5B27A;
            else if (m==MAT_BIRCH_WOOD ? (y%5==1 && (x<9 || x>13)) : (x==7 || x==12 || x==16)) c=shade(c,-45);
            break;
        case PANE:
            on=x>=4 && x<=17 && y>=3 && y<=18 && (x==4||x==17||y==3||y==18||abs(x+y-17)<2|| (m==MAT_ICE && x>12));
            c=shade(base,x+y<19 ? 50 : -15); break;
        case BRICK:
            on=x>=2 && x<=18 && y>=5 && y<=17;
            c=shade(base,(y==11 || (y<11 ? x==9 : x==5||x==14)) ? -55 : y==6||y==12 ? 35 : 0); break;
        case MESH:
            on=x>=2 && x<=18 && y>=4 && y<=17 && (x==2||x==18||y==4||y==17||y%4==0|| (m==MAT_SIEVE && x%4==2));
            c=shade(base,y%4==0 ? 45 : -5); break;
        case ROPE:
            on=(dx*dx+dy*dy<65 && dx*dx+dy*dy>22) || (x>=13 && x<=15 && y>=12);
            c=shade(base,(x+y)%4<2 ? 30 : -30); break;
        case DOOR:
            on=x>=5 && x<=16 && y>=2 && y<=19;
            c=shade(base,x==5||x==16||y==2||y==19||y==11 ? -40 : 20);
            if (x==13 && y==12) c=0xEACA73;
            break;
        case BOARD:
            on=x>=2 && x<=18 && ((y>=7 && y<=10)||(y>=13 && y<=16 && (m==MAT_GRAPHENE || x==5||x==15)));
            c=shade(base,y==7 ? 45 : -10); break;
        case LEAF:
            on=abs(dx+dy/2)+abs(dy)<10;
            c=shade(base,abs(dx+dy/2)<1 ? 55 : dx<0 ? 15 : -25); break;
        case SEED:
            on=(x-7)*(x-7)*2+(y-12)*(y-12)<24 || (x-14)*(x-14)*2+(y-8)*(y-8)<20;
            c=shade(base,(x+y)%5==0 ? 50 : 0); break;
        case STALK:
            on=(x==10 && y>=4 && y<=18)||(y>=4 && y<=12 && abs(dx)==(y%4+1));
            if (m==MAT_WHEAT) on=on||(y>=3 && y<=11 && abs(dx)<=2 && (x+y)%3!=0);
            if (m==MAT_COTTON) on=on||((x-7)*(x-7)+(y-8)*(y-8)<10)||((x-13)*(x-13)+(y-5)*(y-5)<10);
            if (m==MAT_FLOWER) on=on||(dx*dx+(y-6)*(y-6)<14);
            c=x==10 ? 0x799155 : shade(base,x<10 ? 40 : 0); break;
        case FLAME:
            on=y>=3 && abs(dx)<(y<11 ? (y-1)/3 : (20-y)/2+1)+(x<10 ? 1 : 0);
            c=shade(base,abs(dx)<2 && y>10 ? 95 : -10); break;
        case SHELL:
            on=dx*dx+dy*dy<67 && y>=4 && y<=18;
            c=shade(base,y%4==1 ? -50 : x<9 ? 35 : -15); break;
        case RING:
            on=dx*dx+dy*dy<72 && dx*dx+dy*dy>19;
            c=shade(base,x+y<19 ? 45 : -17); break;
        case WEB:
            on=abs(dx)+abs(dy)<13 && (dx==0||dy==0||abs(dx)==abs(dy)||abs(dx)+abs(dy)==7||abs(dx)+abs(dy)==12);
            c=shade(base,-20); break;
        case SPRING:
            on=(y>=13 && y<=17 && abs(dx)<9-abs(y-15))||(y>=4 && y<=14 && abs(dx)<2)||(y>=5 && y<=10 && abs(dx)==(11-y)/2+2);
            c=y>=15 ? 0x356A9B : 0x8BD9EC; break;
        case LAMP:
            on=(dx*dx+(y-8)*(y-8)<37)||(y>=13 && y<=17 && abs(dx)<=3);
            c=y>=13 ? 0x727A86 : x<10 ? 0xFFF6BD : 0xD9B764; break;
        case COIL:
            on=x>=3 && x<=17 && y>=3 && y<=18;
            c=0x394451;
            if (x>=6 && x<=14 && y>=5 && y<=16 && (y%4==1 || (y%8<5 ? x==6 : x==14))) c=shade(base,50);
            if (x==3||y==3) c=0x85939F;
            break;
        case COMB:
            on=abs(dx)+abs(dy)/2<9 && y>=4 && y<=18;
            c=shade(base,((x+(y/4%2)*2)%5<2 && y%4<2) ? -60 : 25); break;
        }
        if (on) p[y*INV_SPR_W+x]=c;
    }
}
