#include <assert.h>
#include <stdio.h>
#include "display_rotation.h"
static void lvgl_pointer(int r,int n,int px,int py,int *x,int *y)
{
    if(r==2 || r==3){px=n-1-px;py=n-1-py;}
    if(r==1 || r==3){int tmp=py;py=px;px=n-1-tmp;}
    *x=px;*y=py;
}
int main(void)
{
    uint16_t pixels[7680];uint8_t visited[960];
    // Every pixel in full-width, asymmetric and tiny partial rectangles must
    // arrive at the physical point which LVGL maps back to that logical pixel.
    const int shapes[][4]={{0,0,480,16},{42,78,34,224},{0,0,8,480},{470,474,10,6},{0,0,2,2}};
    for(int r=0;r<4;r++)for(unsigned s=0;s<sizeof(shapes)/sizeof(shapes[0]);s++) {
        int x0=shapes[s][0],y0=shapes[s][1],w=shapes[s][2],h=shapes[s][3];
        for(int i=0;i<w*h;i++)pixels[i]=i;
        display_rotate_tile(pixels,w,h,r,visited);
        int ax,ay,bx,by;display_to_physical(r,480,x0,y0,&ax,&ay);display_to_physical(r,480,x0+w-1,y0+h-1,&bx,&by);
        int left=ax<bx?ax:bx,top=ay<by?ay:by,physical_width=(r&1)?h:w;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
            int px,py,lx,ly;display_to_physical(r,480,x0+x,y0+y,&px,&py);
            assert(pixels[(py-top)*physical_width+px-left]==y*w+x);
            lvgl_pointer(r,480,px,py,&lx,&ly);assert(lx==x0+x && ly==y0+y);
        }
    }
    puts("Display rotation tests passed: all pixels/touch positions round-trip for 0/90/180/270, full-width and edge tiles");return 0;
}
