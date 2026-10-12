#pragma once
#include <stdint.h>
#include <string.h>
// LVGL pointer rotation is physical -> logical. Scanout must use its inverse.
static inline void display_to_physical(int rotation,int size,int x,int y,int *px,int *py)
{
    switch(rotation) {
        case 1:*px=y;*py=size-1-x;break;
        case 2:*px=size-1-x;*py=size-1-y;break;
        case 3:*px=size-1-y;*py=x;break;
        default:*px=x;*py=y;break;
    }
}
static inline unsigned display_tile_index(unsigned index,int width,int height,int rotation)
{
    unsigned x=index%width,y=index/width;
    if(rotation==1)return (width-1-x)*height+y;
    return x*height+height-1-y;
}
// Rotate a packed RGB565 partial tile in place. A 480x16 tile needs only 960
// bytes of visited bits, no extra framebuffer and no runtime DMA allocation.
static inline void display_rotate_tile(uint16_t *pixels,int width,int height,int rotation,uint8_t *visited)
{
    unsigned count=width*height;
    if(rotation==0)return;
    if(rotation==2){for(unsigned i=0;i<count/2;i++){uint16_t p=pixels[i];pixels[i]=pixels[count-1-i];pixels[count-1-i]=p;}return;}
    memset(visited,0,(count+7)/8);
    for(unsigned start=0;start<count;start++) {
        if(visited[start/8]&(1U<<(start%8)))continue;
        unsigned current=start;uint16_t saved=pixels[current];
        do {
            unsigned next=display_tile_index(current,width,height,rotation);
            uint16_t displaced=pixels[next];pixels[next]=saved;saved=displaced;
            visited[current/8]|=1U<<(current%8);current=next;
        } while(current!=start);
    }
}
