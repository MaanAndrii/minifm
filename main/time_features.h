#pragma once
#include "app.h"
#include <time.h>
static inline time_t alarm_next_once(const alarm_t *a,time_t now)
{
    struct tm local;localtime_r(&now,&local);
    local.tm_hour=a->hour;local.tm_min=a->minute;local.tm_sec=0;local.tm_isdst=-1;
    time_t due=mktime(&local);
    if(due<=now){local.tm_mday++;local.tm_isdst=-1;due=mktime(&local);}
    return due;
}
static inline bool time_in_quiet(uint16_t start,uint16_t end,int minute)
{
    if(start==end)return false;
    return start<end?(minute>=start && minute<end):(minute>=start || minute<end);
}
static inline int time_ramp_volume(int target,int seconds,int64_t elapsed)
{
    if(seconds<=0 || elapsed>=seconds*1000000LL)return target;
    if(elapsed<0)elapsed=0;
    int v=1+(target-1)*elapsed/(seconds*1000000LL);return v;
}
static inline int64_t timer_left(const state_t *s,int64_t now)
{
    return s->timer_paused?s->timer_remaining:s->timer_deadline>now?s->timer_deadline-now:0;
}
