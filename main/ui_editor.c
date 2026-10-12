#include "ui_editor.h"
#include "app.h"
#include "settings_model.h"
#include "time_features.h"
#include "lvgl.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(app_font_18);
LV_FONT_DECLARE(app_font_28);
static lv_obj_t *panel,*heading,*info,*hours,*minutes,*volume,*snooze,*ramp;
static settings_t *settings;
static alarm_t draft;
static int screen,alarm_index,step;
static bool sleep_mode;
static bool timer_menu,timer_exact,timer_running;
static int timer_duration,timer_field,timer_drafts[2];
static lv_obj_t *timer_number,*timer_unit,*timer_summary;
static void alarm_list(void);
static void alarm_form(void);
static void timer_form(void);
bool ui_editor_active(void){return panel!=NULL;}
void ui_editor_close(void){if(panel)lv_obj_delete(panel);panel=NULL;free(settings);settings=NULL;}
static lv_obj_t *caption(lv_obj_t *parent,const char *text,int x,int y,int w,const lv_font_t *font)
{
    lv_obj_t *l=lv_label_create(parent);lv_label_set_text(l,text);lv_obj_set_width(l,w);
    lv_obj_set_pos(l,x,y);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(l,font,0);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);return l;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,lv_event_cb_t cb,int value)
{
    lv_obj_t *b=lv_button_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,52);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x171717),0);lv_obj_set_style_shadow_width(b,0,0);
    lv_obj_set_style_radius(b,12,0);lv_obj_set_style_border_width(b,0,0);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x454545),LV_STATE_CHECKED);
    lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);
    lv_obj_set_style_text_font(l,&app_font_18,0);lv_obj_set_width(l,w-12);
    lv_obj_set_style_text_color(l,lv_color_white(),0);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,(void *)(intptr_t)value);return b;
}
static void create_panel(const char *title)
{
    if(panel)lv_obj_delete(panel);
    panel=lv_obj_create(lv_screen_active());lv_obj_set_pos(panel,24,65);lv_obj_set_size(panel,432,385);
    lv_obj_set_style_bg_color(panel,lv_color_black(),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(panel,0,0);lv_obj_set_style_pad_all(panel,0,0);
    lv_obj_set_style_radius(panel,0,0);lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(panel,lv_color_white(),0);
    heading=caption(panel,title,0,0,432,&app_font_28);
    info=caption(panel,"",12,292,408,&app_font_18);
    lv_obj_set_style_text_color(heading,lv_color_hex(0xaaaaaa),0);
    lv_obj_set_style_text_color(info,lv_color_hex(0xaaaaaa),0);
}
static lv_obj_t *roller(int x,int y,int width,int max,int selected,const lv_font_t *font)
{
    char options[700];int used=0;
    for(int i=0;i<=max;i++)used+=snprintf(options+used,sizeof(options)-used,i==max?"%02d":"%02d\n",i);
    lv_obj_t *r=lv_roller_create(panel);lv_roller_set_options(r,options,LV_ROLLER_MODE_NORMAL);
    lv_obj_set_style_text_font(r,font,0);lv_obj_set_style_text_font(r,font,LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r,lv_color_black(),0);lv_obj_set_style_bg_color(r,lv_color_hex(0x252525),LV_PART_SELECTED);
    lv_obj_set_style_border_width(r,0,0);lv_obj_set_style_radius(r,12,0);
    lv_obj_set_style_pad_all(r,0,0);
    lv_obj_set_style_text_line_space(r,0,0);
    lv_obj_set_style_text_color(r,lv_color_hex(0x888888),0);lv_obj_set_style_text_color(r,lv_color_white(),LV_PART_SELECTED);
    lv_roller_set_visible_row_count(r,3);lv_obj_set_width(r,width);lv_obj_set_pos(r,x,y);
    lv_roller_set_selected(r,selected,LV_ANIM_OFF);return r;
}
static void close_event(lv_event_t *e){(void)e;ui_editor_back();}
static void list_edit(lv_event_t *e)
{
    alarm_index=(int)(intptr_t)lv_event_get_user_data(e);draft=settings->alarms[alarm_index];step=0;screen=2;alarm_form();
}
static void alarm_list(void)
{
    screen=1;create_panel("Будильники");
    lv_obj_t *list=lv_obj_create(panel);lv_obj_set_pos(list,8,44);lv_obj_set_size(list,416,268);
    lv_obj_set_style_bg_color(list,lv_color_black(),0);lv_obj_set_style_border_width(list,0,0);
    lv_obj_set_style_pad_all(list,0,0);lv_obj_set_scroll_dir(list,LV_DIR_VER);
    for(int i=0;i<ALARM_MAX;i++) {
        char text[90];const alarm_t *a=&settings->alarms[i];
        snprintf(text,sizeof(text),"%02u:%02u   %s%s",a->hour,a->minute,a->enabled?"Увімк.":"Вимк.",a->one_shot?" · раз":"");
        button(list,text,4,i*64,388,list_edit,i);
    }
    button(panel,"Назад",116,326,200,close_event,0);
}
void ui_editor_alarms(void)
{
    ui_editor_close();settings=malloc(sizeof(*settings));if(!settings)return;
    app_snapshot(settings,NULL);alarm_list();
}
static void repeat_choice(lv_event_t *e)
{
    int choice=(int)(intptr_t)lv_event_get_user_data(e);draft.one_shot=choice==0;
    if(choice==1)draft.weekdays=127;else if(choice==2)draft.weekdays=62;
    alarm_form();
}
static void day_choice(lv_event_t *e)
{
    int bit=(int)(intptr_t)lv_event_get_user_data(e);draft.weekdays^=1<<bit;draft.one_shot=false;alarm_form();
}
static void enable_choice(lv_event_t *e)
{
    (void)e;draft.hour=lv_roller_get_selected(hours);draft.minute=lv_roller_get_selected(minutes);
    draft.enabled=!draft.enabled;alarm_form();
}
static void volume_change(lv_event_t *e)
{
    draft.volume=lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(info,"Гучність: %u%%",draft.volume);
}
static void collect(void)
{
    if(step==0){draft.hour=lv_roller_get_selected(hours);draft.minute=lv_roller_get_selected(minutes);}
    else if(step==2)draft.station=(int)lv_roller_get_selected(hours)-1;
    else if(step==3){draft.snooze_minutes=lv_roller_get_selected(snooze)+1;draft.ramp_seconds=lv_roller_get_selected(ramp)*10;}
}
static void next_event(lv_event_t *e)
{
    (void)e;collect();
    if(step==1 && !draft.one_shot && !draft.weekdays){lv_label_set_text(info,"Оберіть хоча б один день");return;}
    if(step<3){step++;alarm_form();return;}
    cJSON *j=cJSON_CreateObject(),*a=cJSON_AddObjectToObject(j,"alarm");
    cJSON_AddStringToObject(j,"action","alarm_update");cJSON_AddNumberToObject(j,"index",alarm_index);
    cJSON_AddBoolToObject(a,"enabled",draft.enabled);cJSON_AddNumberToObject(a,"hour",draft.hour);
    cJSON_AddNumberToObject(a,"minute",draft.minute);cJSON_AddNumberToObject(a,"weekdays",draft.weekdays?draft.weekdays:127);
    cJSON_AddNumberToObject(a,"station",draft.station);cJSON_AddBoolToObject(a,"oneShot",draft.one_shot);
    cJSON_AddNumberToObject(a,"volume",draft.volume);cJSON_AddNumberToObject(a,"snooze",draft.snooze_minutes);
    cJSON_AddNumberToObject(a,"ramp",draft.ramp_seconds);
    esp_err_t err=app_command(j);cJSON_Delete(j);
    if(err!=ESP_OK){lv_label_set_text(info,"Не вдалося зберегти");return;}
    app_snapshot(settings,NULL);alarm_list();
}
static void alarm_form(void)
{
    static const char *titles[]={"Час будильника","Повторення","Звук будильника","Гучність і відкладання"};
    create_panel(titles[step]);
    if(step==0) {
        hours=roller(64,84,130,23,draft.hour,&lv_font_montserrat_48);
        minutes=roller(238,84,130,59,draft.minute,&lv_font_montserrat_48);
        caption(panel,":",195,130,40,&lv_font_montserrat_48);
        button(panel,draft.enabled?"Увімкнений":"Вимкнений",106,245,220,enable_choice,0);
    } else if(step==1) {
        lv_obj_t *b=button(panel,"Один раз",8,54,132,repeat_choice,0);if(draft.one_shot)lv_obj_add_state(b,LV_STATE_CHECKED);
        b=button(panel,"Щодня",150,54,132,repeat_choice,1);if(!draft.one_shot && draft.weekdays==127)lv_obj_add_state(b,LV_STATE_CHECKED);
        b=button(panel,"Пн-Пт",292,54,132,repeat_choice,2);if(!draft.one_shot && draft.weekdays==62)lv_obj_add_state(b,LV_STATE_CHECKED);
        static const char *days[]={"Пн","Вт","Ср","Чт","Пт","Сб","Нд"};
        for(int i=0;i<7;i++) {
            b=button(panel,days[i],26+(i%4)*98,135+(i/4)*68,86,day_choice,(i+1)%7);
            if(!draft.one_shot && (draft.weekdays&(1<<((i+1)%7))))lv_obj_add_state(b,LV_STATE_CHECKED);
        }
        lv_label_set_text(info,draft.one_shot?"Найближче спрацювання":"Обрані дні тижня");
    } else if(step==2) {
        char options[1200]="Локальний сигнал";
        for(int i=0;i<settings->station_count;i++){strcat(options,"\n");strcat(options,settings->stations[i].name);}
        hours=lv_roller_create(panel);lv_roller_set_options(hours,options,LV_ROLLER_MODE_NORMAL);
        lv_obj_set_style_text_font(hours,&app_font_28,0);lv_obj_set_style_text_font(hours,&app_font_28,LV_PART_SELECTED);
        lv_obj_set_style_bg_color(hours,lv_color_black(),0);lv_obj_set_style_border_width(hours,0,0);
        lv_obj_set_style_text_line_space(hours,0,0);
        lv_obj_set_style_text_color(hours,lv_color_hex(0x888888),0);
        lv_obj_set_style_text_color(hours,lv_color_white(),LV_PART_SELECTED);
        lv_obj_set_style_bg_color(hours,lv_color_hex(0x252525),LV_PART_SELECTED);
        lv_roller_set_visible_row_count(hours,5);lv_obj_set_width(hours,408);lv_obj_set_pos(hours,12,74);
        lv_roller_set_selected(hours,draft.station+1,LV_ANIM_OFF);
        lv_label_set_text(info,"Без мережі — локальний сигнал");
    } else {
        caption(panel,"Гучність",0,50,432,&app_font_18);
        volume=lv_slider_create(panel);lv_obj_set_pos(volume,40,94);lv_obj_set_size(volume,352,16);
        lv_slider_set_range(volume,1,100);lv_slider_set_value(volume,draft.volume,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(volume,lv_color_hex(0x222222),0);
        lv_obj_set_style_bg_color(volume,lv_color_hex(0xaaaaaa),LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(volume,lv_color_white(),LV_PART_KNOB);
        lv_obj_set_ext_click_area(volume,24);lv_obj_add_event_cb(volume,volume_change,LV_EVENT_VALUE_CHANGED,NULL);
        caption(panel,"Відкласти, хв",16,140,190,&app_font_18);caption(panel,"Наростання, с",222,140,194,&app_font_18);
        snooze=roller(48,164,128,29,draft.snooze_minutes-1,&app_font_28);
        // Display 1..30 for snooze and 0..120 in ten-second increments for ramp.
        char choices[160]="";for(int i=1;i<=30;i++){char part[8];snprintf(part,sizeof(part),i==30?"%02d":"%02d\n",i);strcat(choices,part);}
        lv_roller_set_options(snooze,choices,LV_ROLLER_MODE_NORMAL);lv_roller_set_selected(snooze,draft.snooze_minutes-1,LV_ANIM_OFF);
        ramp=roller(256,164,128,12,draft.ramp_seconds/10,&app_font_28);
        choices[0]=0;for(int i=0;i<=120;i+=10){char part[8];snprintf(part,sizeof(part),i==120?"%d":"%d\n",i);strcat(choices,part);}
        lv_roller_set_options(ramp,choices,LV_ROLLER_MODE_NORMAL);lv_roller_set_selected(ramp,draft.ramp_seconds/10,LV_ANIM_OFF);
        lv_label_set_text_fmt(info,"Гучність: %u%%",draft.volume);
    }
    button(panel,"Назад",12,326,180,close_event,0);
    button(panel,step==3?"Зберегти":"Далі",222,326,198,next_event,0);
}
void ui_editor_back(void)
{
    if(screen==2 && step>0){collect();step--;alarm_form();}
    else if(screen==2)alarm_list();
    else if(screen==3 && timer_exact){timer_exact=false;timer_form();}
    else if(screen==3 && !timer_menu){timer_menu=true;timer_form();}
    else ui_editor_close();
}
static esp_err_t command(const char *action,int value,const char *key)
{
    cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"action",action);
    if(key)cJSON_AddNumberToObject(j,key,value);
    esp_err_t err=app_command(j);cJSON_Delete(j);return err;
}
static void timer_update_value(void)
{
    char text[48];
    if(timer_exact) {
        int n=timer_field==0?timer_duration/3600:timer_field==1?timer_duration/60%60:timer_duration%60;
        snprintf(text,sizeof(text),"%02d",n);
        lv_label_set_text(timer_unit,timer_field==0?"Години":timer_field==1?"Хвилини":"Секунди");
        lv_label_set_text_fmt(timer_summary,"%02d:%02d:%02d",timer_duration/3600,timer_duration/60%60,timer_duration%60);
    } else {
        if(timer_duration%60)snprintf(text,sizeof(text),"%d:%02d",timer_duration/60,timer_duration%60);
        else snprintf(text,sizeof(text),"%d",timer_duration/60);
        lv_label_set_text(timer_unit,timer_duration%60?"хв:с · торкніться для деталей":"хв · торкніться для деталей");
    }
    lv_label_set_text(timer_number,text);timer_drafts[sleep_mode]=timer_duration;
}
static void timer_adjust(lv_event_t *e)
{
    int change=(int)(intptr_t)lv_event_get_user_data(e);
    if(timer_exact) {
        int unit=timer_field==0?3600:timer_field==1?60:1;
        int current=timer_duration/unit;if(timer_field)current%=60;
        int next=current+change,max=timer_field==0?24:59;
        if(next<0)next=0;
        if(next>max)next=max;
        timer_duration+=(next-current)*unit;
        if(timer_duration>86400)timer_duration=86400;
    } else {
        int n=timer_duration+change*60;
        int minimum=timer_duration<60?timer_duration:60;
        if(n<minimum)n=minimum;
        if(n>86400)n=86400;
        timer_duration=n;
    }
    timer_update_value();lv_label_set_text(info,"");
}
static void timer_detail(lv_event_t *e){(void)e;if(!timer_exact){timer_exact=true;timer_field=1;timer_form();}}
static void timer_field_choice(lv_event_t *e){timer_field=(int)(intptr_t)lv_event_get_user_data(e);timer_form();}
static void timer_mode(lv_event_t *e)
{
    sleep_mode=(int)(intptr_t)lv_event_get_user_data(e)!=0;
    timer_menu=false;timer_exact=false;timer_duration=timer_drafts[sleep_mode];timer_form();
}
static void preset(lv_event_t *e)
{
    timer_duration=(int)(intptr_t)lv_event_get_user_data(e)*60;timer_update_value();
}
static void timer_action(lv_event_t *e)
{
    int action=(int)(intptr_t)lv_event_get_user_data(e);esp_err_t err=ESP_OK;
    if(action==0) {
        int n=timer_duration;
        if(!n){lv_label_set_text(timer_exact?timer_summary:timer_unit,"Задайте тривалість");return;}
        err=command(sleep_mode?"sleep":"timer",n,"seconds");
    } else if(action==1)err=command("timer_pause",0,NULL);
    else if(action==2)err=command(sleep_mode?"sleep":"timer",0,"seconds");
    else err=command("timer_repeat",0,NULL);
    if(err!=ESP_OK){lv_label_set_text(info,"Не вдалося застосувати");return;}
    if(action==0 || action==3)ui_editor_close();else timer_form();
}
static lv_obj_t *timer_button(const char *text,int x,int y,int width,int height,lv_event_cb_t callback,int value,const lv_font_t *font)
{
    lv_obj_t *b=button(panel,text,x,y,width,callback,value);lv_obj_set_height(b,height);
    lv_obj_t *label=lv_obj_get_child(b,0);lv_obj_set_style_text_font(label,font,0);lv_obj_center(label);return b;
}
static void timer_form(void)
{
    screen=3;state_t state;app_snapshot(NULL,&state);
    create_panel(timer_menu?"Таймери":sleep_mode?"Таймер сну":"Таймер відліку");
    if(timer_menu) {
        timer_running=false;
        timer_button("Відлік",24,72,384,76,timer_mode,0,&app_font_28);
        timer_button("Сон",24,166,384,76,timer_mode,1,&app_font_28);
        if(state.timer_seconds)button(panel,"Повторити відлік",24,258,384,timer_action,3);
        button(panel,"Закрити",116,326,200,close_event,0);return;
    }
    int64_t left=sleep_mode?state.sleep_deadline-esp_timer_get_time():timer_left(&state,esp_timer_get_time());
    timer_running=left>0;
    if(timer_running) {
        info=caption(panel,"",0,114,432,&lv_font_montserrat_48);
        caption(panel,sleep_mode?"Плавне вимкнення радіо":state.timer_paused?"Відлік призупинено":"Залишилося часу",0,184,432,&app_font_18);
        if(!sleep_mode)timer_button(state.timer_paused?"Продовжити":"Пауза",24,230,384,64,timer_action,1,&app_font_28);
        ui_editor_refresh();
    } else {
        int y=timer_exact?126:92;
        if(timer_exact) {
            static const char *names[]={"Год","Хв","Сек"};
            for(int i=0;i<3;i++) {
                lv_obj_t *b=timer_button(names[i],12+i*140,50,128,56,timer_field_choice,i,&app_font_28);
                if(timer_field==i)lv_obj_set_style_bg_color(b,lv_color_hex(0x454545),0);
            }
        }
        lv_obj_t *minus=timer_button("-",18,y+6,88,72,timer_adjust,-1,&lv_font_montserrat_48);
        lv_obj_add_event_cb(minus,timer_adjust,LV_EVENT_LONG_PRESSED_REPEAT,(void *)(intptr_t)-1);
        lv_obj_t *plus=timer_button("+",326,y+6,88,72,timer_adjust,1,&lv_font_montserrat_48);
        lv_obj_add_event_cb(plus,timer_adjust,LV_EVENT_LONG_PRESSED_REPEAT,(void *)(intptr_t)1);
        lv_obj_t *number=timer_button("",122,y,188,84,timer_detail,0,&lv_font_montserrat_48);
        lv_obj_set_style_bg_color(number,lv_color_black(),0);timer_number=lv_obj_get_child(number,0);
        timer_unit=caption(panel,"",0,y+98,432,timer_exact?&app_font_28:&app_font_18);
        if(timer_exact)timer_summary=caption(panel,"",0,258,432,&app_font_28);
        else {
            timer_button("5 хв",12,238,128,60,preset,5,&app_font_28);
            timer_button("15 хв",152,238,128,60,preset,15,&app_font_28);
            timer_button("30 хв",292,238,128,60,preset,30,&app_font_28);
        }
        timer_update_value();
    }
    timer_button("Назад",12,326,198,56,close_event,0,&app_font_28);
    timer_button(timer_running?"Скасувати":"Старт",222,326,198,56,timer_action,timer_running?2:0,&app_font_28);
}
void ui_editor_timer(void)
{
    ui_editor_close();state_t state;app_snapshot(NULL,&state);
    timer_drafts[0]=state.timer_seconds?state.timer_seconds:300;timer_drafts[1]=1800;
    timer_exact=false;timer_menu=true;sleep_mode=false;
    int64_t now=esp_timer_get_time();
    if(timer_left(&state,now)>0)timer_menu=false;
    else if(state.sleep_deadline>now){timer_menu=false;sleep_mode=true;}
    timer_duration=timer_drafts[sleep_mode];timer_form();
}
void ui_editor_refresh(void)
{
    if(!panel || screen!=3 || !timer_running || timer_menu)return;
    state_t s;app_snapshot(NULL,&s);int64_t now=esp_timer_get_time();
    int64_t left=sleep_mode?s.sleep_deadline-now:timer_left(&s,now);
    int n=left>0?(left+999999)/1000000:0;
    char text[32];snprintf(text,sizeof(text),"%02d:%02d:%02d",n/3600,n/60%60,n%60);
    if(strcmp(lv_label_get_text(info),text))lv_label_set_text(info,text);
    if(!n)timer_form();
}
