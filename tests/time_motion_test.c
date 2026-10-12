#include <assert.h>
#include <stdio.h>
#include "settings_model.h"
#include "time_features.h"
#include "motion_logic.h"
int main(void)
{
    settings_v1_t old={.version=1,.station_count=1,.volume=42,.brightness=67};
    strcpy(old.ssid,"saved-wifi");strcpy(old.password,"saved-password");strcpy(old.token,"0123456789abcdef0123456789abcdef");
    strcpy(old.timezone,"UTC0");strcpy(old.stations[0].url,"https://radio.example/live");
    old.alarms[3]=(alarm_v1_t){.enabled=true,.hour=8,.minute=15,.weekdays=62,.station=0};
    settings_t s;assert(settings_migrate_v1(&s,&old));
    assert(s.version==2 && s.volume==42 && s.brightness==67 && s.station_count==1);
    assert(!strcmp(s.ssid,old.ssid) && !strcmp(s.password,old.password) && !strcmp(s.token,old.token));
    assert(!strcmp(s.stations[0].url,old.stations[0].url));assert(s.alarms[3].enabled && s.alarms[3].hour==8 && s.alarms[3].minute==15);
    assert(s.alarms[3].station==0 && s.alarms[3].volume==42 && s.alarms[3].snooze_minutes==9 && !s.alarms[3].one_shot);
    assert(s.auto_rotate && s.lift_wake && s.night_start==1380);old.version=9;assert(!settings_migrate_v1(&s,&old));
    assert(time_in_quiet(1380,420,0));assert(time_in_quiet(1380,420,1380));assert(!time_in_quiet(1380,420,420));
    assert(!time_in_quiet(0,0,100));assert(time_in_quiet(600,660,610));assert(!time_in_quiet(600,660,100));
    assert(time_ramp_volume(60,30,0)==1);assert(time_ramp_volume(60,30,15000000)==30);assert(time_ramp_volume(60,30,30000000)==60);
    state_t timer={.timer_deadline=5000000};assert(timer_left(&timer,2000000)==3000000);assert(timer_left(&timer,6000000)==0);
    timer.timer_paused=true;timer.timer_remaining=1234567;assert(timer_left(&timer,99999999)==1234567);
    motion_filter_t f={0};int64_t now=0;int lifts=0;
    for(int i=0;i<50;i++,now+=50000)lifts+=motion_feed(&f,.008f,-.006f,1,now);
    assert(lifts==0 && f.rotation==0 && f.armed);
    for(int i=0;i<10;i++,now+=50000)lifts+=motion_feed(&f,0,-1,0,now);
    assert(lifts==1);for(int i=0;i<30;i++,now+=50000)lifts+=motion_feed(&f,0,-1,0,now);
    assert(lifts==1 && f.rotation==0);
    // Brief vibration cannot turn the display; sustained orientations cover all sides.
    for(int i=0;i<2;i++,now+=50000)motion_feed(&f,1,0,0,now);assert(f.rotation==0);
    for(int i=0;i<40;i++,now+=50000)motion_feed(&f,1,0,0,now);assert(f.rotation==1);
    for(int i=0;i<40;i++,now+=50000)motion_feed(&f,0,1,0,now);assert(f.rotation==2);
    for(int i=0;i<40;i++,now+=50000)motion_feed(&f,-1,0,0,now);assert(f.rotation==3);
    for(int i=0;i<40;i++,now+=50000)motion_feed(&f,0,0,1,now);assert(f.rotation==3);
    motion_filter_t upright={0};lifts=0;
    for(int i=0;i<50;i++,now+=50000)motion_feed(&upright,0,0,1,now);
    for(int i=0;i<10;i++,now+=50000)lifts+=motion_feed(&upright,0,0,1.3f,now);
    assert(lifts==1); // A pickup acceleration can wake without a large tilt.
    puts("time/motion tests passed: NVS v1 migration, quiet hours, volume ramp, paused timer, pickup filtering and four rotations");
    return 0;
}
