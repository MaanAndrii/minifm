#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
typedef enum { UI_TAP, UI_IGNORE, UI_PAGE_NEXT, UI_PAGE_PREV, UI_STATION_NEXT,
    UI_STATION_PREV, UI_VOLUME_UP, UI_VOLUME_DOWN } ui_gesture_t;
static inline ui_gesture_t ui_gesture(int dx,int dy,int start_x,int page)
{
    if(abs(dx)>=60 && abs(dx)>abs(dy))return dx<0?UI_PAGE_NEXT:UI_PAGE_PREV;
    if(page==1 && abs(dy)>=60 && abs(dy)>abs(dx))
        return start_x>=360?(dy<0?UI_VOLUME_UP:UI_VOLUME_DOWN):(dy<0?UI_STATION_NEXT:UI_STATION_PREV);
    return abs(dx)>20 || abs(dy)>20?UI_IGNORE:UI_TAP;
}
static inline int ui_idle_brightness(int normal,int64_t idle,bool ringing)
{
    if(ringing)return normal;
    if(idle>=60000000)return 0;
    if(idle>=20000000)return normal<8?normal:8;
    return normal;
}
