#include "app.h"
#include "ui_input.h"
#include "power_status.h"
#include "motion.h"
#include "time_features.h"
#include "ui_editor.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "esp_timer.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "driver/gpio.h"

LV_FONT_DECLARE(app_font_18);
LV_FONT_DECLARE(app_font_96);
LV_FONT_DECLARE(app_font_160);
LV_FONT_DECLARE(app_font_28);
static lv_obj_t *title,*large,*subtitle,*footer,*dots,*access;
static lv_obj_t *dot[4];
static lv_obj_t *wifi_icon,*battery_icon,*supply_icon;
static settings_t *cfg;
static int page,selected,brightness=-1;
static int64_t last_input,dots_until,pressed_at;
static int64_t lift_until;
static lv_point_t pressed_point;
static bool blank,wake_only,access_open,was_ringing;
typedef struct {int pin,raw,stable;int64_t changed,down;bool wake;} ui_key_t;
static ui_key_t keys[]={{.pin=18,.raw=1,.stable=1},{.pin=0,.raw=1,.stable=1}};

static void command(const char *action,int value,const char *key)
{
    cJSON *j=cJSON_CreateObject();if(!j)return;
    cJSON_AddStringToObject(j,"action",action);
    if(key)cJSON_AddNumberToObject(j,key,value);
    app_command(j);cJSON_Delete(j);
}
static void set_brightness(int value)
{
    if(value!=brightness && bsp_display_brightness_set(value)==ESP_OK)brightness=value;
}
static void wake(void)
{
    last_input=esp_timer_get_time();blank=false;set_brightness(cfg->brightness);
    lift_until=0;
}
static void change_page(int step)
{
    page=(page+step+4)%4;dots_until=esp_timer_get_time()+2500000;
}
static void toggle(const state_t *s)
{
    if(s->ringing)command("snooze",0,NULL);
    else if(s->mode==AUDIO_RADIO)command("pause",0,NULL);
    else if(cfg->station_count)command("play",selected,"station");
}
static void show_access(void)
{
    char net[80],text[512];network_status(net,sizeof(net));
    snprintf(text,sizeof(text),"%s\n\nПароль точки доступу\n%.16s\n\nКлюч вебінтерфейсу\n%.16s\n%s\n\nТоркніться, щоб закрити",net,cfg->token,cfg->token,cfg->token+16);
    lv_label_set_text(access,text);access_open=true;
    lv_obj_remove_flag(access,LV_OBJ_FLAG_HIDDEN);
}
static void touch(lv_event_t *event)
{
    lv_indev_t *input=lv_indev_active();if(!input)return;
    if(lv_event_get_code(event)==LV_EVENT_PRESSED) {
        wake_only=blank;wake();pressed_at=esp_timer_get_time();
        lv_indev_get_point(input,&pressed_point);return;
    }
    if(lv_event_get_code(event)!=LV_EVENT_RELEASED)return;
    lv_point_t end;lv_indev_get_point(input,&end);
    int dx=end.x-pressed_point.x,dy=end.y-pressed_point.y;
    wake();if(wake_only){wake_only=false;return;}
    if(access_open){access_open=false;lv_obj_add_flag(access,LV_OBJ_FLAG_HIDDEN);return;}
    state_t s;app_snapshot(NULL,&s);
    if(s.ringing) {command(esp_timer_get_time()-pressed_at>=1000000?"dismiss":"snooze",0,NULL);return;}
    ui_gesture_t gesture=ui_gesture(dx,dy,pressed_point.x,page);
    if(gesture==UI_PAGE_NEXT || gesture==UI_PAGE_PREV) {change_page(gesture==UI_PAGE_NEXT?1:-1);return;}
    if(gesture>=UI_STATION_NEXT) {
        if(gesture==UI_VOLUME_UP || gesture==UI_VOLUME_DOWN) {
            int v=s.volume+(gesture==UI_VOLUME_UP?5:-5);command("volume",v<0?0:v>100?100:v,"value");
        } else if(cfg->station_count) {
            selected=(selected+(dy<0?1:-1)+cfg->station_count)%cfg->station_count;
            command("play",selected,"station");
        }
        return;
    }
    if(gesture!=UI_TAP)return;
    if(esp_timer_get_time()-pressed_at>=1200000 && page==0)show_access();
    else if(page==1)toggle(&s);
    else if(page==2)ui_editor_alarms();
    else if(page==3)ui_editor_timer();
}
static void poll_keys(int64_t now,const state_t *s)
{
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++) {
        ui_key_t *k=&keys[i];int level=gpio_get_level(k->pin);
        if(level!=k->raw){k->raw=level;k->changed=now;}
        if(level==k->stable || now-k->changed<30000)continue;
        k->stable=level;
        if(!level){k->down=now;k->wake=blank;wake();}
        else {
            bool held=now-k->down>=1000000;wake();
            if(k->wake){k->wake=false;continue;}
            if(access_open){access_open=false;lv_obj_add_flag(access,LV_OBJ_FLAG_HIDDEN);continue;}
            if(s->ringing)command(held?"dismiss":"snooze",0,NULL);
            else if(ui_editor_active()){if(i==1){if(held)ui_editor_close();else ui_editor_back();}}
            else if(i==0){if(held)show_access();else toggle(s);}
            else {if(held){page=0;dots_until=now+2500000;}else change_page(1);}
        }
    }
}
static void label_text(lv_obj_t *label,const char *text)
{
    if(strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);
}
static void indicators(void)
{
    int rssi;int net=network_indicator(&rssi);
    label_text(wifi_icon,net==2?LV_SYMBOL_WIFI " AP":net==1?LV_SYMBOL_WIFI:LV_SYMBOL_WIFI " x");
    lv_color_t color=lv_color_hex(net==1?(rssi>=-70?0xffffff:0x999999):net==2?0x999999:0x444444);
    if(!lv_color_eq(lv_obj_get_style_text_color(wifi_icon,0),color))lv_obj_set_style_text_color(wifi_icon,color,0);
    power_status_t p;power_snapshot(&p);
    const char *battery="?";
    if(p.valid) {
        if(!p.battery_present)battery="";
        else if(p.percent<0)battery=LV_SYMBOL_BATTERY_EMPTY " ?";
        else if(p.percent>=85)battery=LV_SYMBOL_BATTERY_FULL;
        else if(p.percent>=60)battery=LV_SYMBOL_BATTERY_3;
        else if(p.percent>=35)battery=LV_SYMBOL_BATTERY_2;
        else if(p.percent>=15)battery=LV_SYMBOL_BATTERY_1;
        else battery=LV_SYMBOL_BATTERY_EMPTY;
    }
    char battery_text[40];
    if(p.valid && p.battery_present && p.percent>=0)snprintf(battery_text,sizeof(battery_text),"%s %d%%",battery,p.percent);
    else snprintf(battery_text,sizeof(battery_text),"%s",battery);
    label_text(battery_icon,battery_text);
    color=lv_color_hex(p.valid && p.battery_present && p.percent>=0 && p.percent<15?0xff6655:0xaaaaaa);
    if(!lv_color_eq(lv_obj_get_style_text_color(battery_icon,0),color))lv_obj_set_style_text_color(battery_icon,color,0);
    label_text(supply_icon,p.valid && p.usb_present?(p.charging?LV_SYMBOL_USB " " LV_SYMBOL_CHARGE:LV_SYMBOL_USB):"");
}
static void render(const state_t *s)
{
    time_t wall=time(NULL);struct tm local;localtime_r(&wall,&local);
    char value[64],sub[160]="",bottom[96]="";
    const char *heading="";
    bool big=true;
    if(s->ringing) {
        heading=s->timer_signal?"ТАЙМЕР ЗАВЕРШЕНО":"БУДИЛЬНИК";strftime(value,sizeof(value),"%H:%M",&local);
        snprintf(sub,sizeof(sub),"%s",s->timer_signal?"Торкніться, щоб вимкнути":"Натискання - відкласти\nУтримання - вимкнути");
    } else if(page==0) {
        if(s->time_valid)strftime(value,sizeof(value),"%H:%M",&local);else strcpy(value,"--:--");
        if(s->mode==AUDIO_RADIO && !s->paused && s->station>=0 && s->station<cfg->station_count)
            snprintf(bottom,sizeof(bottom),"%s",cfg->stations[s->station].name);
    } else if(page==1) {
        heading="РАДІО";big=false;
        snprintf(value,sizeof(value),"%s",cfg->station_count?cfg->stations[selected].name:"Немає станцій");
        const char *status=s->mode!=AUDIO_RADIO?"Зупинено":s->paused?"Пауза":!strncmp(s->message,"Playing ",8)?"Грає":"Підключення";
        snprintf(sub,sizeof(sub),"%s   |   %d%%",status,s->volume);
    } else if(page==2) {
        heading="БУДИЛЬНИК";int best=-1,best_minutes=8*1440;
        for(int i=0;i<ALARM_MAX;i++) {
            const alarm_t *a=&cfg->alarms[i];if(!a->enabled)continue;
            if(a->one_shot) {
                time_t due=a->once_at>wall?a->once_at:alarm_next_once(a,wall);
                int minutes=(due-wall+59)/60;if(minutes<best_minutes){best=i;best_minutes=minutes;}continue;
            }
            for(int day=0;day<=7;day++) {
                if(!(a->weekdays&(1<<((local.tm_wday+day)%7))))continue;
                int minutes=day*1440+a->hour*60+a->minute-local.tm_hour*60-local.tm_min;
                if(minutes<=0)continue;
                if(minutes<best_minutes){best=i;best_minutes=minutes;}break;
            }
        }
        if(best<0){strcpy(value,"Вимкнений");big=false;}
        else {
            const alarm_t *a=&cfg->alarms[best];snprintf(value,sizeof(value),"%02u:%02u",a->hour,a->minute);
            static const char *days[]={"Нд","Пн","Вт","Ср","Чт","Пт","Сб"};
            for(int d=1;d<=7;d++)if(a->weekdays&(1<<(d%7))) {
                if(sub[0])strcat(sub,"  ");
                strcat(sub,days[d%7]);
            }
            if(a->one_shot)snprintf(sub,sizeof(sub),"Один раз");
            if(!s->time_valid)snprintf(bottom,sizeof(bottom),"Очікуємо синхронізації часу");
        }
        if(!bottom[0])snprintf(bottom,sizeof(bottom),"Торкніться для налаштування");
    } else {
        heading="ТАЙМЕР";int64_t now=esp_timer_get_time();int n=(timer_left(s,now)+999999)/1000000;
        if(n>0) {
            if(n>=3600)snprintf(value,sizeof(value),"%02d:%02d",n/3600,n/60%60);
            else snprintf(value,sizeof(value),"%02d:%02d",n/60,n%60);
            snprintf(sub,sizeof(sub),"%s%s",s->timer_paused?"Пауза":"Відлік",n>=3600?" · год:хв":" · хв:с");
        } else {snprintf(value,sizeof(value),"--:--");snprintf(sub,sizeof(sub),"Торкніться для налаштування");}
        if(s->sleep_deadline>now) {
            int remaining=(s->sleep_deadline-now+59999999)/60000000;
            snprintf(bottom,sizeof(bottom),"Сон через %d хв",remaining);
        }
    }
    label_text(title,heading);label_text(large,value);label_text(subtitle,sub);label_text(footer,bottom);
    const lv_font_t *font=big?(page==0 && !s->ringing?&app_font_160:&app_font_96):&app_font_28;
    if(lv_obj_get_style_text_font(large,0)!=font)lv_obj_set_style_text_font(large,font,0);
    // Shift static text once a minute to distribute AMOLED pixel wear.
    int shift=(local.tm_min%3-1)*3;
    lv_obj_align(large,LV_ALIGN_CENTER,shift,-16+shift);
    lv_obj_align(subtitle,LV_ALIGN_CENTER,0,90+shift);
    static int dot_page=-1;
    if(dot_page!=page){for(int i=0;i<4;i++)lv_obj_set_style_bg_color(dot[i],lv_color_hex(i==page?0xffffff:0x444444),0);dot_page=page;}
    if(s->ringing || esp_timer_get_time()>dots_until)lv_obj_add_flag(dots,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(dots,LV_OBJ_FLAG_HIDDEN);
}
static void refresh(lv_timer_t *timer)
{
    (void)timer;state_t s;app_snapshot(cfg,&s);
    if(s.station>=0 && s.station<cfg->station_count)selected=s.station;
    if(selected>=cfg->station_count)selected=0;
    int64_t now=esp_timer_get_time();poll_keys(now,&s);
    bool finger=false;
    for(lv_indev_t *in=lv_indev_get_next(NULL);in;in=lv_indev_get_next(in))
        if(lv_indev_get_type(in)==LV_INDEV_TYPE_POINTER && lv_indev_get_state(in)==LV_INDEV_STATE_PRESSED)finger=true;
    if(ui_editor_active() && finger)wake();
    if(s.ringing){if(!was_ringing){ui_editor_close();access_open=false;lv_obj_add_flag(access,LV_OBJ_FLAG_HIDDEN);}wake();}
    was_ringing=s.ringing;
    motion_status_t m;motion_snapshot(&m);static uint32_t seen_lift;
    bool lifted=m.lifts!=seen_lift;seen_lift=m.lifts;
    if(m.valid && lifted && blank && cfg->lift_wake && !s.ringing) {
        time_t wall=time(NULL);struct tm local;localtime_r(&wall,&local);
        bool quiet=cfg->night_quiet && (!s.time_valid || time_in_quiet(cfg->night_start,cfg->night_end,local.tm_hour*60+local.tm_min));
        if(!quiet){wake();page=0;lift_until=now+15000000;}
    }
    static int applied_rotation;
    int rotation=!cfg->auto_rotate?0:m.valid?(m.rotation+cfg->rotation_offset)%4:applied_rotation;
    if(rotation!=applied_rotation && !finger && !ui_editor_active() && !access_open) {
        // Keep panel scanout at its native orientation. The partial-tile flush
        // applies exactly the inverse of LVGL's physical-to-logical touch rotation.
        lv_display_set_rotation(lv_display_get_default(),(lv_display_rotation_t)rotation);
        lv_obj_invalidate(lv_screen_active());applied_rotation=rotation;
    }
    int64_t idle=now-last_input;
    blank=(idle>=60000000 || (lift_until && now>=lift_until)) && !s.ringing;
    if(blank && ui_editor_active())ui_editor_close();
    set_brightness(blank?0:ui_idle_brightness(cfg->brightness,idle,s.ringing));
    static int64_t last_render;
    if(!blank && now-last_render>=250000){if(ui_editor_active())ui_editor_refresh();else render(&s);last_render=now;}
    static int64_t last_indicators;
    if(!blank && now-last_indicators>=2000000){indicators();last_indicators=now;}
}
static lv_obj_t *label(lv_obj_t *root,int width)
{
    lv_obj_t *l=lv_label_create(root);lv_obj_set_width(l,width);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_label_set_text(l,"");
    lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE);return l;
}
void ui_start(void)
{
    cfg=malloc(sizeof(*cfg));if(!cfg)return;app_snapshot(cfg,NULL);
    const gpio_config_t gpio_cfg={.pin_bit_mask=(1ULL<<18)|(1ULL<<0),.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    lv_obj_t *root=lv_screen_active();
    lv_obj_set_style_bg_color(root,lv_color_black(),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(root,lv_color_white(),0);lv_obj_set_style_text_font(root,&app_font_18,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(root,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root,touch,LV_EVENT_PRESSED,NULL);lv_obj_add_event_cb(root,touch,LV_EVENT_RELEASED,NULL);
    wifi_icon=label(root,100);battery_icon=label(root,140);supply_icon=label(root,64);
    lv_obj_set_style_text_font(wifi_icon,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_font(battery_icon,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_font(supply_icon,&lv_font_montserrat_24,0);
    lv_obj_align(wifi_icon,LV_ALIGN_TOP_LEFT,48,25);
    lv_obj_align(battery_icon,LV_ALIGN_TOP_RIGHT,-48,25);
    lv_obj_align(supply_icon,LV_ALIGN_TOP_RIGHT,-194,25);
    title=label(root,400);lv_obj_align(title,LV_ALIGN_TOP_MID,0,60);
    lv_obj_set_style_text_color(title,lv_color_hex(0x888888),0);
    large=label(root,456);subtitle=label(root,400);
    footer=label(root,432);lv_obj_align(footer,LV_ALIGN_BOTTOM_MID,0,-52);
    dots=lv_obj_create(root);lv_obj_remove_style_all(dots);lv_obj_set_size(dots,78,8);
    lv_obj_remove_flag(dots,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dots,LV_ALIGN_BOTTOM_MID,0,-30);
    for(int i=0;i<4;i++) {
        dot[i]=lv_obj_create(dots);lv_obj_remove_style_all(dot[i]);
        lv_obj_set_size(dot[i],6,6);lv_obj_set_pos(dot[i],i*24,0);
        lv_obj_remove_flag(dot[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(dot[i],LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_opa(dot[i],LV_OPA_COVER,0);
    }
    access=label(root,420);lv_obj_set_size(access,420,380);lv_obj_center(access);
    lv_obj_set_style_bg_color(access,lv_color_black(),0);lv_obj_set_style_bg_opa(access,LV_OPA_COVER,0);
    lv_obj_add_flag(access,LV_OBJ_FLAG_HIDDEN);
    wake();dots_until=last_input+2500000;state_t s;app_snapshot(NULL,&s);render(&s);
    lv_timer_create(refresh,30,NULL);esp_lv_adapter_unlock();
}
