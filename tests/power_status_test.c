#include "power_status.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    power_status_t p=power_decode(0x20,0,100);
    assert(p.valid && p.usb_present && !p.battery_present && p.percent==-1 && !p.charging);
    p=power_decode(0x28,0x20,62);
    assert(p.battery_present && p.usb_present && p.charging && p.percent==62);
    p=power_decode(8,0x40,25);
    assert(p.battery_present && !p.usb_present && !p.charging && p.percent==25);
    p=power_decode(0x28,0x28,50);assert(!p.usb_present && !p.charging);
    p=power_decode(0x28,0,255);assert(p.percent==-1 && !p.charging);
    p=power_decode(8,0,0);assert(p.percent==0);
    puts("Power tests passed: USB-only, charging, discharging, invalid VBUS, unknown SOC");
    return 0;
}
