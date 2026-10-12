#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
// Windows native harness; firmware uses the ESP-IDF POSIX functions.
struct tm *localtime_r(const time_t *v,struct tm *out){*out=*localtime(v);return out;}
static int setenv(const char *name,const char *value,int overwrite){return 0;}
void tzset(void){}
#include "../main/controller.c"
settings_t g_settings;
int64_t fake_now=1000000;
static bool fail_save;
esp_err_t settings_save(void){return fail_save?ESP_ERR_INVALID_STATE:ESP_OK;}
bool audio_record_busy(void){return false;}
void power_snapshot(power_status_t *out){memset(out,0,sizeof(*out));out->percent=-1;}
int network_indicator(int *rssi){*rssi=-50;return 1;}
static void send(const char *json)
{
    cJSON *j=cJSON_Parse(json);assert(j);assert(app_command(j)==ESP_OK);cJSON_Delete(j);
}
static void tick(void)
{
    time_t wall=1791709200;struct tm local={.tm_year=126,.tm_mon=9,.tm_mday=11,.tm_wday=0,.tm_hour=7,.tm_min=0};
    scheduler_tick(wall,&local,fake_now);
}
int main(void)
{
    settings_new_defaults(&g_settings);g_settings.volume=35;g_settings.station_count=1;
    strcpy(g_settings.stations[0].name,"Saved");strcpy(g_settings.stations[0].url,"https://saved.example/live");
    state.volume=35;send("{\"action\":\"play\",\"station\":0}");
    uint32_t before=request.generation;
    send("{\"action\":\"timer\",\"seconds\":60}");fake_now+=10000000;
    send("{\"action\":\"timer_pause\"}");assert(state.timer_paused && state.timer_remaining==50000000);
    fake_now+=120000000;tick();assert(!state.ringing && timer_left(&state,fake_now)==50000000);
    send("{\"action\":\"timer_pause\"}");assert(!state.timer_paused && state.timer_deadline==fake_now+50000000);
    fake_now+=50000000;tick();assert(state.ringing && state.timer_signal && request.mode==AUDIO_TONE);
    send("{\"action\":\"dismiss\"}");assert(!state.ringing && request.mode==AUDIO_RADIO && request.station==0 && request.generation>before);
    assert(!strcmp(request.source,"https://saved.example/live"));
    send("{\"action\":\"pause\"}");assert(state.paused);
    send("{\"action\":\"timer_repeat\"}");fake_now+=60000000;tick();assert(state.timer_signal);
    send("{\"action\":\"snooze\"}");assert(request.mode==AUDIO_RADIO && state.paused && !state.ringing);
    send("{\"action\":\"timer\",\"seconds\":0}");assert(!state.timer_deadline && !state.timer_paused);
    send("{\"action\":\"alarm_update\",\"index\":2,\"alarm\":{\"enabled\":true,\"hour\":7,\"minute\":0,\"weekdays\":1,\"station\":-1,\"volume\":70,\"snooze\":5,\"ramp\":30,\"oneShot\":false}}");
    tick();assert(state.ringing && !state.timer_signal && ringing_alarm.volume==70);
    audio_request_t req;int vol;app_audio_request(&req,&vol);assert(vol==1);fake_now+=30000000;app_audio_request(&req,&vol);assert(vol==70);
    send("{\"action\":\"snooze\"}");assert(snooze_at==fake_now+300000000 && !state.ringing);
    fake_now+=299000000;tick();assert(!state.ringing);fake_now+=1000000;tick();assert(state.ringing);
    send("{\"action\":\"dismiss\"}");tick();assert(!state.ringing); // Same calendar minute cannot retrigger.
    g_settings.alarms[2].enabled=false;
    g_settings.alarms[4]=alarm_default(45);g_settings.alarms[4].enabled=true;g_settings.alarms[4].one_shot=true;g_settings.alarms[4].once_at=1791709200;
    tick();assert(state.ringing && !g_settings.alarms[4].enabled);send("{\"action\":\"dismiss\"}");tick();assert(!state.ringing);
    send("{\"action\":\"play\",\"station\":0}");send("{\"action\":\"sleep\",\"seconds\":60}");
    fake_now+=45000000;app_audio_request(&req,&vol);assert(vol==17);fake_now+=15000000;tick();assert(request.mode==AUDIO_IDLE && !state.sleep_deadline);
    send("{\"action\":\"play\",\"station\":0}");send("{\"action\":\"sleep\",\"seconds\":30}");send("{\"action\":\"timer\",\"seconds\":10}");
    fake_now+=10000000;tick();assert(state.ringing && restore_audio);fake_now+=20000000;tick();
    send("{\"action\":\"dismiss\"}");assert(request.mode==AUDIO_IDLE); // Sleep expiry wins over radio restoration.
    fail_save=true;int brightness=g_settings.brightness;
    cJSON *j=cJSON_Parse("{\"action\":\"configure\",\"brightness\":88}");assert(app_command(j)!=ESP_OK);cJSON_Delete(j);assert(g_settings.brightness==brightness);
    fail_save=false;
    j=cJSON_Parse("{\"action\":\"alarm_update\",\"index\":8}");assert(app_command(j)==ESP_ERR_INVALID_ARG);cJSON_Delete(j);
    puts("controller tests passed: countdown pause/resume, repeat, interrupted radio restore, alarm volume/snooze, one-shot, sleep fade and failed-save rollback");
    return 0;
}
