#include "app.h"
#include "core.h"
#include "power_status.h"
#include "audio_pause.h"
#include "settings_model.h"
#include "time_features.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "nvs.h"
static SemaphoreHandle_t mutex;
static state_t state;
static audio_request_t request;
static int64_t last_alarm_day[ALARM_MAX];
static int64_t snooze_at, ring_until;
static int snooze_station=-1;
static alarm_t ringing_alarm;
static int64_t ring_started;
static audio_request_t interrupted;
static bool restore_audio;
static int64_t sleep_fade=30000000;
static void save_alarm_history(void)
{
    nvs_handle_t nvs;if(nvs_open("radio",NVS_READWRITE,&nvs)!=ESP_OK)return;
    if(nvs_set_blob(nvs,"alarm_days",last_alarm_day,sizeof(last_alarm_day))==ESP_OK)nvs_commit(nvs);
    nvs_close(nvs);
}
void app_lock(void) { xSemaphoreTake(mutex,portMAX_DELAY); }
void app_unlock(void) { xSemaphoreGive(mutex); }
void app_snapshot(settings_t *cfg,state_t *s) { app_lock(); if(cfg) *cfg=g_settings; if(s) *s=state; app_unlock(); }
static void set_audio(audio_mode_t mode,int station,const char *source)
{
    request.mode=mode; request.station=station; request.paused=false;
    snprintf(request.source,sizeof(request.source),"%s",source?source:""); request.generation++;
    state.mode=mode; state.station=station; state.paused=false; state.mic_peak=0;
    state.recording_bytes=0; state.recording[0]=0; state.message[0]=0;
}
static void finish_ring(bool restore)
{
    state.ringing=false;state.timer_signal=false;snooze_at=0;
    if(restore && restore_audio && interrupted.mode==AUDIO_RADIO) {
        set_audio(AUDIO_RADIO,interrupted.station,interrupted.source);
        request.paused=interrupted.paused;state.paused=interrupted.paused;
    } else set_audio(AUDIO_IDLE,-1,NULL);
    restore_audio=false;
}
static void ring(int station)
{
    state.ringing=true; snooze_station=station;
    ring_started=esp_timer_get_time();ring_until=ring_started+600LL*1000000;
    if(station>=0 && station<g_settings.station_count) set_audio(AUDIO_RADIO,station,g_settings.stations[station].url);
    else set_audio(AUDIO_TONE,-1,NULL);
}
void app_audio_request(audio_request_t *r,int *volume)
{
    app_lock();*r=request;int64_t now=esp_timer_get_time();*volume=state.volume;
    if(state.ringing)*volume=time_ramp_volume(ringing_alarm.volume,ringing_alarm.ramp_seconds,now-ring_started);
    else if(state.sleep_deadline && request.mode==AUDIO_RADIO) {
        int64_t left=state.sleep_deadline-now;
        if(left<sleep_fade)*volume=left>0?state.volume*left/sleep_fade:0;
    }
    app_unlock();
}
void app_audio_status(uint32_t gen,const char *msg,int peak,uint32_t bytes)
{
    app_lock(); if(request.generation==gen) { snprintf(state.message,sizeof(state.message),"%s",msg?msg:""); state.mic_peak=peak; state.recording_bytes=bytes; }
    app_unlock();
}
void app_audio_finished(uint32_t gen,const char *msg)
{
    app_lock(); if(request.generation==gen) { set_audio(AUDIO_IDLE,-1,NULL); snprintf(state.message,sizeof(state.message),"%s",msg); } app_unlock();
}
void app_sd_ready(bool ready) { app_lock(); state.sd_ready=ready; app_unlock(); }
static bool number(const cJSON *j,const char *key,int min,int max,int *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if(!cJSON_IsNumber(v) || v->valuedouble!=v->valueint || v->valueint<min || v->valueint>max) return false;
    *out=v->valueint; return true;
}
static bool text(const cJSON *j,const char *key,char *dest,size_t size)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if(!cJSON_IsString(v) || strlen(v->valuestring)>=size) return false;
    for(const char *p=v->valuestring;*p;p++) if((unsigned char)*p<32) return false;
    strcpy(dest,v->valuestring); return true;
}
static bool read_alarm(const cJSON *row,alarm_t *a,int count)
{
    alarm_t old=*a;
    int n;const cJSON *v=cJSON_GetObjectItemCaseSensitive(row,"enabled");
    if(!cJSON_IsBool(v))return false;
    a->enabled=cJSON_IsTrue(v);
    if(!number(row,"hour",0,23,&n))return false;
    a->hour=n;
    if(!number(row,"minute",0,59,&n))return false;
    a->minute=n;
    if(!number(row,"weekdays",1,127,&n))return false;
    a->weekdays=n;
    if(!number(row,"station",-1,count-1,&n))return false;
    a->station=n;
    if(cJSON_HasObjectItem(row,"volume")){if(!number(row,"volume",1,100,&n))return false;a->volume=n;}
    if(cJSON_HasObjectItem(row,"snooze")){if(!number(row,"snooze",1,30,&n))return false;a->snooze_minutes=n;}
    if(cJSON_HasObjectItem(row,"ramp")){if(!number(row,"ramp",0,120,&n))return false;a->ramp_seconds=n;}
    if(cJSON_HasObjectItem(row,"oneShot")) {
        v=cJSON_GetObjectItemCaseSensitive(row,"oneShot");if(!cJSON_IsBool(v))return false;a->one_shot=cJSON_IsTrue(v);
    }
    // Re-arm for the next occurrence only when relevant fields change or enabled again.
    if(a->one_shot && a->enabled) {
        if(!old.enabled || !old.one_shot || old.hour!=a->hour || old.minute!=a->minute || !old.once_at)
            a->once_at=alarm_next_once(a,time(NULL));
    } else a->once_at=0;
    return true;
}
static bool schedule_changed(const alarm_t *a,const alarm_t *b)
{
    return a->enabled!=b->enabled || a->hour!=b->hour || a->minute!=b->minute ||
        a->weekdays!=b->weekdays || a->one_shot!=b->one_shot;
}
static esp_err_t configure(const cJSON *j)
{
    settings_t *next=malloc(sizeof(*next)); if(!next) return ESP_ERR_NO_MEM;
    *next=g_settings; int n;
    const char *flags[]={"autoRotate","liftWake","nightQuiet"};
    bool *dest[]={&next->auto_rotate,&next->lift_wake,&next->night_quiet};
    for(int i=0;i<3;i++)if(cJSON_HasObjectItem(j,flags[i])) {
        const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,flags[i]);if(!cJSON_IsBool(v))goto invalid;*dest[i]=cJSON_IsTrue(v);
    }
    if(cJSON_HasObjectItem(j,"nightStart")){if(!number(j,"nightStart",0,1439,&n))goto invalid;next->night_start=n;}
    if(cJSON_HasObjectItem(j,"nightEnd")){if(!number(j,"nightEnd",0,1439,&n))goto invalid;next->night_end=n;}
    if(cJSON_HasObjectItem(j,"rotationOffset")){if(!number(j,"rotationOffset",0,3,&n))goto invalid;next->rotation_offset=n;}
    if(cJSON_HasObjectItem(j,"brightness")) { if(!number(j,"brightness",1,100,&n)) goto invalid; next->brightness=n; }
    if(cJSON_HasObjectItem(j,"timezone") && !text(j,"timezone",next->timezone,sizeof(next->timezone))) goto invalid;
    if(!next->timezone[0]) goto invalid;
    if(cJSON_HasObjectItem(j,"ssid") && !text(j,"ssid",next->ssid,sizeof(next->ssid))) goto invalid;
    if(cJSON_HasObjectItem(j,"password") && !text(j,"password",next->password,sizeof(next->password))) goto invalid;
    if(next->password[0] && strlen(next->password)<8) goto invalid;
    const cJSON *array=cJSON_GetObjectItemCaseSensitive(j,"stations");
    if(array) {
        if(!cJSON_IsArray(array) || cJSON_GetArraySize(array)>STATION_MAX) goto invalid;
        next->station_count=cJSON_GetArraySize(array); memset(next->stations,0,sizeof(next->stations));
        for(int i=0;i<next->station_count;i++) {
            const cJSON *row=cJSON_GetArrayItem(array,i); station_t *s=&next->stations[i];
            if(!text(row,"name",s->name,sizeof(s->name)) || !s->name[0] || !text(row,"url",s->url,sizeof(s->url))) goto invalid;
            if(strncmp(s->url,"http://",7) && strncmp(s->url,"https://",8)) goto invalid;
        }
    }
    array=cJSON_GetObjectItemCaseSensitive(j,"alarms");
    if(array) {
        if(!cJSON_IsArray(array) || cJSON_GetArraySize(array)>ALARM_MAX) goto invalid;
        for(int i=cJSON_GetArraySize(array);i<ALARM_MAX;i++)next->alarms[i]=alarm_default(next->volume);
        for(int i=0;i<cJSON_GetArraySize(array);i++) {
            const cJSON *row=cJSON_GetArrayItem(array,i); alarm_t *a=&next->alarms[i];
            if(!read_alarm(row,a,next->station_count))goto invalid;
        }
    }
    for(int i=0;i<ALARM_MAX;i++) if(next->alarms[i].station>=next->station_count) next->alarms[i].station=-1;
    settings_t *old=malloc(sizeof(*old)); if(!old) {free(next);return ESP_ERR_NO_MEM;} *old=g_settings;
    bool timezone_changed=strcmp(next->timezone,old->timezone)!=0;
    g_settings=*next; esp_err_t err=settings_save();
    if(err!=ESP_OK) g_settings=*old;
    else {
        if(timezone_changed) {setenv("TZ",g_settings.timezone,1);tzset();}
        bool history_changed=false;
        for(int i=0;i<ALARM_MAX;i++)if(schedule_changed(&old->alarms[i],&next->alarms[i])){last_alarm_day[i]=-1;history_changed=true;}
        if(history_changed)save_alarm_history();
        // An active radio URL is copied in request and survives station list edits.
        if(request.station>=g_settings.station_count) { request.station=-1; state.station=-1; }
    }
    free(old); free(next); return err;
invalid:
    free(next); return ESP_ERR_INVALID_ARG;
}
esp_err_t app_command(const cJSON *j)
{
    const cJSON *action=cJSON_GetObjectItemCaseSensitive(j,"action");
    if(!cJSON_IsString(action)) return ESP_ERR_INVALID_ARG;
    const char *a=action->valuestring; int n; esp_err_t err=ESP_OK;
    app_lock();
    if(!strcmp(a,"play")) { if(!number(j,"station",0,g_settings.station_count-1,&n)) err=ESP_ERR_INVALID_ARG; else {state.ringing=false;snooze_at=0;set_audio(AUDIO_RADIO,n,g_settings.stations[n].url);} }
    else if(!strcmp(a,"stop") || !strcmp(a,"dismiss")) { finish_ring(!strcmp(a,"dismiss")); }
    else if(!strcmp(a,"volume")) {
        if(!number(j,"value",0,100,&n)) err=ESP_ERR_INVALID_ARG;
        else {int old=g_settings.volume;g_settings.volume=n;err=settings_save();if(err!=ESP_OK)g_settings.volume=old;state.volume=g_settings.volume;}
    }
    else if(!strcmp(a,"timer") || !strcmp(a,"sleep")) {
        if(!number(j,"seconds",0,86400,&n)) err=ESP_ERR_INVALID_ARG;
        else {int64_t deadline=n?esp_timer_get_time()+n*1000000LL:0;
            if(!strcmp(a,"timer")){state.timer_deadline=deadline;state.timer_remaining=0;state.timer_paused=false;if(n)state.timer_seconds=n;}
            else {state.sleep_deadline=deadline;sleep_fade=n>0 && n<30?n*1000000LL:30000000;}}
    }
    else if(!strcmp(a,"timer_pause")) {
        int64_t now=esp_timer_get_time();
        if(state.timer_paused){state.timer_deadline=now+state.timer_remaining;state.timer_remaining=0;state.timer_paused=false;}
        else if(state.timer_deadline>now){state.timer_remaining=state.timer_deadline-now;state.timer_deadline=0;state.timer_paused=true;}
        else err=ESP_ERR_INVALID_STATE;
    }
    else if(!strcmp(a,"timer_repeat")) {
        if(!state.timer_seconds)err=ESP_ERR_INVALID_STATE;
        else {if(state.ringing && state.timer_signal)finish_ring(true);state.timer_deadline=esp_timer_get_time()+state.timer_seconds*1000000LL;state.timer_paused=false;state.timer_remaining=0;}
    }
    else if(!strcmp(a,"alarm_update")) {
        if(!number(j,"index",0,ALARM_MAX-1,&n))err=ESP_ERR_INVALID_ARG;
        else {alarm_t old=g_settings.alarms[n],next=old;
            if(!read_alarm(cJSON_GetObjectItemCaseSensitive(j,"alarm"),&next,g_settings.station_count))err=ESP_ERR_INVALID_ARG;
            else {g_settings.alarms[n]=next;err=settings_save();if(err!=ESP_OK)g_settings.alarms[n]=old;
                else if(schedule_changed(&old,&next)){last_alarm_day[n]=-1;save_alarm_history();}}
        }
    }
    else if(!strcmp(a,"record")) {
        if(!state.sd_ready || state.ringing || state.mode==AUDIO_RECORD || audio_record_busy()) err=ESP_ERR_INVALID_STATE;
        else set_audio(AUDIO_RECORD,-1,NULL);
    }
    else if(!strcmp(a,"pause")) {
        if(request.mode!=AUDIO_RECORD && request.mode!=AUDIO_RADIO) err=ESP_ERR_INVALID_STATE;
        else {
            audio_toggle_pause(&request,&state);
            if(request.mode==AUDIO_RADIO) {
                // Invalidate the active connection on both pause and resume.
                // Keep the station and copied URL for a fresh live connection.
                snprintf(state.message,sizeof(state.message),"%s",request.paused?"Radio paused":"Connecting to radio");
            }
        }
    }
    else if(!strcmp(a,"file")) {
        char name[96]; if(!text(j,"name",name,sizeof(name)) || !core_recording_name(name)) err=ESP_ERR_INVALID_ARG;
        else if(!state.sd_ready) err=ESP_ERR_INVALID_STATE;
        else {state.ringing=false;snooze_at=0;set_audio(AUDIO_FILE,-1,name);}
    }
    else if(!strcmp(a,"snooze")) {
        if(!state.ringing) err=ESP_ERR_INVALID_STATE;
        else if(state.timer_signal)finish_ring(true);
        else {state.ringing=false;snooze_at=esp_timer_get_time()+ringing_alarm.snooze_minutes*60LL*1000000;set_audio(AUDIO_IDLE,-1,NULL);}
    }
    else if(!strcmp(a,"configure")) err=configure(j);
    else err=ESP_ERR_INVALID_ARG;
    app_unlock(); return err;
}
static cJSON *settings_json(bool public_status)
{
    cJSON *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"ssid",g_settings.ssid); cJSON_AddStringToObject(j,"timezone",g_settings.timezone);
    cJSON_AddNumberToObject(j,"brightness",g_settings.brightness);
    cJSON_AddBoolToObject(j,"autoRotate",g_settings.auto_rotate);cJSON_AddBoolToObject(j,"liftWake",g_settings.lift_wake);
    cJSON_AddBoolToObject(j,"nightQuiet",g_settings.night_quiet);cJSON_AddNumberToObject(j,"nightStart",g_settings.night_start);
    cJSON_AddNumberToObject(j,"nightEnd",g_settings.night_end);cJSON_AddNumberToObject(j,"rotationOffset",g_settings.rotation_offset);
    cJSON *stations=cJSON_AddArrayToObject(j,"stations");
    for(int i=0;i<g_settings.station_count;i++) { cJSON *s=cJSON_CreateObject();cJSON_AddStringToObject(s,"name",g_settings.stations[i].name);if(!public_status)cJSON_AddStringToObject(s,"url",g_settings.stations[i].url);cJSON_AddItemToArray(stations,s); }
    cJSON *alarms=cJSON_AddArrayToObject(j,"alarms");
    for(int i=0;i<ALARM_MAX;i++) {alarm_t *a=&g_settings.alarms[i];cJSON *s=cJSON_CreateObject();cJSON_AddBoolToObject(s,"enabled",a->enabled);cJSON_AddNumberToObject(s,"hour",a->hour);cJSON_AddNumberToObject(s,"minute",a->minute);cJSON_AddNumberToObject(s,"weekdays",a->weekdays?a->weekdays:127);cJSON_AddNumberToObject(s,"station",a->station);cJSON_AddNumberToObject(s,"volume",a->volume);cJSON_AddNumberToObject(s,"snooze",a->snooze_minutes);cJSON_AddNumberToObject(s,"ramp",a->ramp_seconds);cJSON_AddBoolToObject(s,"oneShot",a->one_shot);cJSON_AddItemToArray(alarms,s);}
    return j;
}
cJSON *app_settings_json(void) {app_lock();cJSON *j=settings_json(false);app_unlock();return j;}
cJSON *app_status_json(void)
{
    app_lock(); cJSON *j=settings_json(true);int64_t now=esp_timer_get_time();
    cJSON_AddNumberToObject(j,"mode",state.mode);cJSON_AddNumberToObject(j,"volume",state.volume);cJSON_AddNumberToObject(j,"station",state.station);
    cJSON_AddBoolToObject(j,"paused",state.paused);cJSON_AddBoolToObject(j,"ringing",state.ringing);cJSON_AddBoolToObject(j,"sd",state.sd_ready);
    power_status_t power;power_snapshot(&power);
    cJSON_AddBoolToObject(j,"powerKnown",power.valid);
    cJSON_AddBoolToObject(j,"batteryPresent",power.valid && power.battery_present);
    if(power.valid && power.percent>=0)cJSON_AddNumberToObject(j,"batteryPercent",power.percent);
    else cJSON_AddNullToObject(j,"batteryPercent");
    cJSON_AddBoolToObject(j,"usbPower",power.valid && power.usb_present);
    cJSON_AddBoolToObject(j,"charging",power.valid && power.charging);
    int rssi;int net=network_indicator(&rssi);
    cJSON_AddBoolToObject(j,"wifiConnected",net==1);cJSON_AddBoolToObject(j,"wifiAp",net==2);
    if(net==1)cJSON_AddNumberToObject(j,"wifiRssi",rssi);else cJSON_AddNullToObject(j,"wifiRssi");
    cJSON_AddBoolToObject(j,"timeValid",state.time_valid);cJSON_AddNumberToObject(j,"epoch",time(NULL));
    cJSON_AddNumberToObject(j,"timer",(timer_left(&state,now)+999999)/1000000);
    cJSON_AddBoolToObject(j,"timerPaused",state.timer_paused);cJSON_AddBoolToObject(j,"timerSignal",state.timer_signal);
    cJSON_AddNumberToObject(j,"snoozeMinutes",ringing_alarm.snooze_minutes);
    cJSON_AddNumberToObject(j,"sleep",state.sleep_deadline>now?(state.sleep_deadline-now)/1000000:0);
    cJSON_AddNumberToObject(j,"recordingSeconds",state.recording_bytes/32000);cJSON_AddNumberToObject(j,"micPeak",state.mic_peak);
    cJSON_AddStringToObject(j,"message",state.message);app_unlock();return j;
}
static void scheduler_tick(time_t wall,const struct tm *local,int64_t now)
{
        state.time_valid=wall>1704067200;
        if(state.time_valid) for(int i=0;i<ALARM_MAX;i++) {
            alarm_t *a=&g_settings.alarms[i];bool due=false;
            if(a->enabled && a->one_shot) {
                if(!a->once_at || a->once_at<1704067200){a->once_at=alarm_next_once(a,wall);settings_save();}
                if(wall>=a->once_at){due=wall-a->once_at<600;a->enabled=false;settings_save();}
            } else due=core_alarm_due(local,a->hour,a->minute,a->weekdays,a->enabled,&last_alarm_day[i]);
            if(due){save_alarm_history();ringing_alarm=*a;restore_audio=false;state.timer_signal=false;snooze_at=0;ring(a->station);}
        }
        if(snooze_at && now>=snooze_at) {snooze_at=0;ring(snooze_station);}
        if(state.sleep_deadline && now>=state.sleep_deadline) {
            state.sleep_deadline=0;
            // A countdown alert must not resume a radio whose sleep timer expired.
            restore_audio=false;
            if(request.mode==AUDIO_RADIO && !state.ringing)set_audio(AUDIO_IDLE,-1,NULL);
        }
        if(state.timer_deadline && now>=state.timer_deadline && !state.ringing && !snooze_at) {
            state.timer_deadline=0;interrupted=request;restore_audio=request.mode==AUDIO_RADIO;
            ringing_alarm=alarm_default(state.volume);ringing_alarm.ramp_seconds=0;state.timer_signal=true;ring(-1);
        }
        if(state.ringing && now>=ring_until)finish_ring(true);
}
static void scheduler(void *arg)
{
    (void)arg;
    for(;;) {
        time_t wall=time(NULL);struct tm local;localtime_r(&wall,&local);int64_t now=esp_timer_get_time();
        app_lock();scheduler_tick(wall,&local,now);app_unlock();vTaskDelay(pdMS_TO_TICKS(200));
    }
}
void app_init(void)
{
    mutex=xSemaphoreCreateMutex();configASSERT(mutex);state.volume=g_settings.volume;state.station=-1;
    for(int i=0;i<ALARM_MAX;i++)last_alarm_day[i]=-1;
    nvs_handle_t nvs;if(nvs_open("radio",NVS_READONLY,&nvs)==ESP_OK){size_t size=sizeof(last_alarm_day);nvs_get_blob(nvs,"alarm_days",last_alarm_day,&size);nvs_close(nvs);}
    setenv("TZ",g_settings.timezone,1);tzset();
    configASSERT(xTaskCreate(scheduler,"scheduler",4096,NULL,4,NULL)==pdPASS);
}
