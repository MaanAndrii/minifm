#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool valid,battery_present,usb_present,charging;
    int percent; // -1 means unavailable, including absent battery.
} power_status_t;
static inline power_status_t power_decode(uint8_t status1,uint8_t status2,int percent)
{
    power_status_t p={.valid=true,.battery_present=(status1&8)!=0,
        .usb_present=(status1&32)!=0 && (status2&8)==0,.percent=-1};
    p.charging=p.battery_present && p.usb_present && (status2>>5)==1;
    if(p.battery_present && percent>=0 && percent<=100)p.percent=percent;
    return p;
}
void power_start(void);
void power_snapshot(power_status_t *status);
