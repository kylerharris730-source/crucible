#pragma once
#include "item.h"

/* Presentation only: endpoints come from the existing melee reach/pose. */
void drawHeldSpear(u32* pixels,int width,int height,ItemId item,
                   float x0,float y0,float x1,float y1,
                   u32 (*lighting)(u32,int,int)=0);
