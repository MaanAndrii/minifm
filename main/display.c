#include "app.h"
#include "display_rotation.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"

static uint8_t rotation_visited[(480*16+7)/8];
static esp_err_t rotated_bitmap(lv_display_t *display,esp_lcd_panel_handle_t panel,
    int x1,int y1,int x2,int y2,const void *pixels,void *ctx)
{
    (void)ctx;int rotation=lv_display_get_rotation(display);
    if(rotation==0)return esp_lcd_panel_draw_bitmap(panel,x1,y1,x2,y2,pixels);
    int width=x2-x1,height=y2-y1;
    if(width<=0 || height<=0 || width*height>480*16)return ESP_ERR_INVALID_SIZE;
    display_rotate_tile((uint16_t *)pixels,width,height,rotation,rotation_visited);
    int ax,ay,bx,by;
    display_to_physical(rotation,480,x1,y1,&ax,&ay);
    display_to_physical(rotation,480,x2-1,y2-1,&bx,&by);
    int left=ax<bx?ax:bx,top=ay<by?ay:by;
    int right=ax>bx?ax:bx,bottom=ay>by?ay:by;
    return esp_lcd_panel_draw_bitmap(panel,left,top,right+1,bottom+1,pixels);
}

static void round_area(lv_event_t *event)
{
    lv_area_t *area=lv_event_get_param(event);
    area->x1 &= ~1; area->y1 &= ~1;
    area->x2 |= 1; area->y2 |= 1;
}

bool display_start(void)
{
    bsp_display_cfg_t cfg={
        .lv_adapter_cfg=ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation=ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode=ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags={.swap_xy=1,.mirror_x=0,.mirror_y=1}
    };
    if(esp_lv_adapter_init(&cfg.lv_adapter_cfg)!=ESP_OK)return false;
    esp_lcd_panel_handle_t panel=NULL;
    esp_lcd_panel_io_handle_t io=NULL;
    const bsp_display_config_t panel_cfg={.max_transfer_sz=BSP_LCD_H_RES*16*2};
    if(bsp_display_new(&panel_cfg,&panel,&io)!=ESP_OK)return false;
    // PSRAM draw buffers require a fresh internal DMA staging allocation per SPI
    // transfer. Keep one small, aligned SRAM buffer instead, allocated before Wi-Fi.
    const esp_lv_adapter_display_config_t display_cfg={
        .panel=panel,.panel_io=io,
        .profile={.interface=ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation=cfg.rotation,.hor_res=BSP_LCD_H_RES,.ver_res=BSP_LCD_V_RES,
            .buffer_height=16,.use_psram=false,.enable_ppa_accel=false,
            .require_double_buffer=false},
        .tear_avoid_mode=cfg.tear_avoid_mode
    };
    lv_display_t *display=esp_lv_adapter_register_display(&display_cfg);
    if(!display)return false;
    const esp_lv_adapter_draw_bitmap_callbacks_t callbacks={.custom_draw_bitmap=rotated_bitmap};
    if(esp_lv_adapter_set_draw_bitmap_callbacks(display,&callbacks,NULL)!=ESP_OK)return false;
    lv_display_add_event_cb(display,round_area,LV_EVENT_INVALIDATE_AREA,NULL);
    esp_lcd_touch_handle_t touch=NULL;
    if(bsp_touch_new(&cfg,&touch)!=ESP_OK)return false;
    const esp_lv_adapter_touch_config_t touch_cfg=ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display,touch);
    if(!esp_lv_adapter_register_touch(&touch_cfg))return false;
    if(bsp_display_brightness_init()!=ESP_OK)return false;
    if(esp_lv_adapter_start()!=ESP_OK)return false;
    ESP_LOGI("display","SRAM draw buffer: 15360 bytes; DMA free=%u, largest=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    return true;
}
