#include "ui_input.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(ui_gesture(-60,10,200,0)==UI_PAGE_NEXT);
    assert(ui_gesture(60,10,200,2)==UI_PAGE_PREV);
    assert(ui_gesture(10,-60,200,1)==UI_STATION_NEXT);
    assert(ui_gesture(10,60,200,1)==UI_STATION_PREV);
    assert(ui_gesture(10,-60,360,1)==UI_VOLUME_UP);
    assert(ui_gesture(10,60,479,1)==UI_VOLUME_DOWN);
    assert(ui_gesture(0,80,200,0)==UI_IGNORE);
    assert(ui_gesture(60,60,200,1)==UI_IGNORE);
    assert(ui_gesture(20,20,200,1)==UI_TAP);
    assert(ui_gesture(21,0,200,1)==UI_IGNORE);
    assert(ui_idle_brightness(70,19999999,false)==70);
    assert(ui_idle_brightness(70,20000000,false)==8);
    assert(ui_idle_brightness(3,20000000,false)==3);
    assert(ui_idle_brightness(70,60000000,false)==0);
    assert(ui_idle_brightness(70,60000000,true)==70);
    puts("UI input tests passed: swipes, tap rejection, dimming, blanking, alarm wake");
    return 0;
}
