#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/ui_editor.c"
#include "display_rotation.h"
settings_t g_settings;
static state_t fake_state;
int64_t fake_now;
static cJSON *last_command;
void app_snapshot(settings_t *cfg,state_t *state){if(cfg)*cfg=g_settings;if(state)*state=fake_state;}
esp_err_t app_command(const cJSON *j){cJSON_Delete(last_command);last_command=cJSON_Duplicate(j,true);return ESP_OK;}
static uint16_t framebuffer[480*480];
static uint32_t ticks;
static uint32_t tick(void){return ticks;}
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *px)
{
    uint16_t *data=(uint16_t *)px;for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++)framebuffer[y*480+x]=*data++;
    lv_display_flush_ready(d);
}
static void snapshot(const char *name)
{
    lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL);char path[160];snprintf(path,sizeof(path),".tools/screenshots/%s.ppm",name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n480 480\n255\n");
    for(int i=0;i<480*480;i++){uint16_t p=framebuffer[i];unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);
}
static void check_buttons(lv_obj_t *obj)
{
    if(lv_obj_check_type(obj,&lv_button_class)) {
        assert(lv_obj_get_height(obj)>=48);assert(lv_obj_get_width(obj)>=76);
        lv_area_t a;lv_obj_get_coords(obj,&a);
        if(lv_obj_get_parent(obj)==panel){assert(a.y2<450);assert(a.x1>=24 && a.x2<456);}
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++)check_buttons(lv_obj_get_child(obj,i));
}
static void verify(const char *name)
{
    snapshot(name);check_buttons(panel);
    assert(lv_color_eq(lv_obj_get_style_text_color(panel,0),lv_color_white()));
    if(screen==2 && step==0)assert(lv_obj_get_y(hours)+lv_obj_get_height(hours)<=245);
    if(screen==2 && step==3)assert(lv_obj_get_y(snooze)+lv_obj_get_height(snooze)<292);
    if(screen==3 && !timer_menu && !timer_running) {
        assert(lv_obj_get_height(lv_obj_get_parent(timer_number))>=80);
        assert(lv_obj_get_y(timer_unit)>=180);
    }
}
static lv_obj_t *find_button(lv_obj_t *parent,const char *text)
{
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t *child=lv_obj_get_child(parent,i);
        if(lv_obj_check_type(child,&lv_button_class) && !strcmp(lv_label_get_text(lv_obj_get_child(child,0)),text))return child;
    }
    assert(!"button not found");return NULL;
}
static bool finger;
static lv_point_t physical;
static void read_pointer(lv_indev_t *in,lv_indev_data_t *data){(void)in;data->point=physical;data->state=finger?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
static void click(lv_indev_t *in,const char *text,int rotation)
{
    lv_obj_update_layout(panel);lv_area_t a;lv_obj_get_coords(find_button(panel,text),&a);
    int px,py;display_to_physical(rotation,480,(a.x1+a.x2)/2,(a.y1+a.y2)/2,&px,&py);
    physical.x=px;physical.y=py;finger=true;ticks+=30;lv_indev_read(in);
    finger=false;ticks+=30;lv_indev_read(in);
}
int main(void)
{
    lv_init();lv_tick_set_cb(tick);lv_display_t *d=lv_display_create(480,480);
    static uint16_t buffer[480*480];lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
    lv_obj_t *root=lv_screen_active();lv_obj_set_style_bg_color(root,lv_color_black(),0);lv_obj_set_style_text_color(root,lv_color_white(),0);lv_obj_set_style_text_font(root,&app_font_18,0);
    lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(&app_font_18,&glyph,0x2014,0));assert(!glyph.is_placeholder);
    assert(lv_font_get_glyph_dsc(&app_font_28,&glyph,0xB7,0));assert(!glyph.is_placeholder);
    settings_new_defaults(&g_settings);g_settings.station_count=1;strcpy(g_settings.stations[0].name,"Тернопіль FM");
    ui_editor_alarms();verify("device-alarm-list");
    alarm_index=0;draft=settings->alarms[0];screen=2;step=0;alarm_form();verify("device-alarm-time");
    lv_roller_set_selected(hours,8,LV_ANIM_OFF);lv_roller_set_selected(minutes,45,LV_ANIM_OFF);next_event(NULL);
    assert(draft.hour==8 && draft.minute==45 && step==1);verify("device-alarm-days");
    draft.one_shot=true;next_event(NULL);verify("device-alarm-sound");lv_roller_set_selected(hours,1,LV_ANIM_OFF);next_event(NULL);
    assert(draft.station==0 && step==3);verify("device-alarm-volume");
    lv_roller_set_selected(snooze,4,LV_ANIM_OFF);lv_roller_set_selected(ramp,6,LV_ANIM_OFF);next_event(NULL);
    const cJSON *a=cJSON_GetObjectItemCaseSensitive(last_command,"alarm");assert(cJSON_GetObjectItemCaseSensitive(a,"hour")->valueint==8);
    assert(cJSON_GetObjectItemCaseSensitive(a,"snooze")->valueint==5 && cJSON_GetObjectItemCaseSensitive(a,"ramp")->valueint==60);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a,"oneShot")));
    // Closing a draft sends no save command.
    cJSON_Delete(last_command);last_command=NULL;screen=2;step=0;alarm_form();ui_editor_back();ui_editor_back();assert(!last_command && !panel);
    ui_editor_timer();verify("device-timer-menu");
    lv_indev_t *in=lv_indev_create();lv_indev_set_type(in,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(in,read_pointer);lv_indev_set_display(in,d);
    click(in,"Відлік",0);verify("device-timer-setup");
    // Actual LVGL pointer events target the same buttons in all four rotations.
    for(int r=0;r<4;r++) {
        lv_display_set_rotation(d,r);timer_duration=300;timer_update_value();
        click(in,"+",r);assert(timer_duration==360);click(in,"-",r);assert(timer_duration==300);
    }
    lv_display_set_rotation(d,LV_DISPLAY_ROTATION_0);click(in,"15 хв",0);assert(timer_duration==900);
    timer_detail(NULL);verify("device-timer-detail");
    timer_field=2;timer_form();click(in,"+",0);assert(timer_duration==901);
    ui_editor_back();assert(!timer_exact && timer_duration==901);
    click(in,"+",0);assert(timer_duration==961);click(in,"-",0);assert(timer_duration==901);
    click(in,"Старт",0);
    assert(!panel && cJSON_GetObjectItemCaseSensitive(last_command,"seconds")->valueint==901);
    fake_state.timer_deadline=900000000;ui_editor_timer();verify("device-timer-running");
    fake_state.timer_paused=true;fake_state.timer_remaining=123000000;timer_form();verify("device-timer-paused");
    fake_state.sleep_deadline=1800000000;sleep_mode=true;timer_form();verify("device-sleep-timer");
    fake_state=(state_t){0};ui_editor_timer();click(in,"Сон",0);assert(sleep_mode && timer_duration==1800);
    click(in,"5 хв",0);click(in,"Старт",0);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(last_command,"action")->valuestring,"sleep"));
    assert(cJSON_GetObjectItemCaseSensitive(last_command,"seconds")->valueint==300);
    ui_editor_close();cJSON_Delete(last_command);
    lv_obj_clean(lv_screen_active());
    LV_FONT_DECLARE(app_font_160);
    const char *times[]={"00:00","11:11","23:59","88:88","--:--"};
    for(int i=0;i<5;i++) {
        lv_point_t size;lv_text_get_size(&size,times[i],&app_font_160,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        assert(size.x<=450);
    }
    lv_obj_t *clock=lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(clock,&app_font_160,0);lv_obj_set_style_text_color(clock,lv_color_white(),0);
    lv_obj_set_width(clock,456);lv_obj_set_style_text_align(clock,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_text(clock,"23:59");lv_obj_align(clock,LV_ALIGN_CENTER,0,-16);snapshot("device-large-clock");
    puts("LVGL tests passed: real pointer clicks in four rotations, large timer controls, minute presets, exact seconds, both timer modes and alarm draft/save/cancel");return 0;
}
