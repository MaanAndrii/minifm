#pragma once
#include "app.h"
#include <stddef.h>
#include <string.h>
typedef struct { bool enabled; uint8_t hour,minute,weekdays; int station; } alarm_v1_t;
typedef struct {
    uint32_t version; char ssid[33],password[65],token[33],timezone[96];
    int volume,brightness,station_count; station_t stations[STATION_MAX];
    alarm_v1_t alarms[ALARM_MAX];
} settings_v1_t;
static inline alarm_t alarm_default(int volume)
{
    return (alarm_t){.hour=7,.weekdays=62,.station=-1,.volume=volume>0?volume:35,
        .snooze_minutes=9,.ramp_seconds=30};
}
static inline void settings_new_defaults(settings_t *s)
{
    s->version=2;s->auto_rotate=true;s->lift_wake=true;s->night_quiet=true;
    s->night_start=23*60;s->night_end=7*60;s->rotation_offset=0;
    for(int i=0;i<ALARM_MAX;i++)s->alarms[i]=alarm_default(s->volume);
}
static inline bool settings_migrate_v1(settings_t *s,const settings_v1_t *old)
{
    if(old->version!=1 || old->station_count<0 || old->station_count>STATION_MAX)return false;
    memset(s,0,sizeof(*s));memcpy(s,old,offsetof(settings_v1_t,alarms));
    settings_new_defaults(s);
    for(int i=0;i<ALARM_MAX;i++) {
        s->alarms[i].enabled=old->alarms[i].enabled;s->alarms[i].hour=old->alarms[i].hour;
        s->alarms[i].minute=old->alarms[i].minute;s->alarms[i].weekdays=old->alarms[i].weekdays;
        s->alarms[i].station=old->alarms[i].station;
    }
    return true;
}
