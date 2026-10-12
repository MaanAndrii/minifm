#include "app.h"
#include <stddef.h>
#include "settings_model.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_random.h"
settings_t g_settings;
void settings_init(void)
{
    // Never erase NVS automatically: a version/partition mismatch needs deliberate recovery.
    ESP_ERROR_CHECK(nvs_flash_init());
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("radio",NVS_READWRITE,&nvs));
    size_t size=0;
    esp_err_t err=nvs_get_blob(nvs,"settings",NULL,&size);
    bool migrated=false;
    if(err==ESP_OK && size==sizeof(g_settings))err=nvs_get_blob(nvs,"settings",&g_settings,&size);
    else if(err==ESP_OK && size==sizeof(settings_v1_t)) {
        settings_v1_t *old=malloc(size);ESP_ERROR_CHECK(old?ESP_OK:ESP_ERR_NO_MEM);
        err=nvs_get_blob(nvs,"settings",old,&size);
        if(err==ESP_OK)migrated=settings_migrate_v1(&g_settings,old);
        free(old);
    }
    nvs_close(nvs);
    if(migrated){ESP_ERROR_CHECK(settings_save());return;}
    if(err==ESP_OK && size==sizeof(g_settings) && g_settings.version==2)return;
    // Do not overwrite an unrecognised existing configuration.
    if(err!=ESP_ERR_NVS_NOT_FOUND)ESP_ERROR_CHECK(ESP_ERR_INVALID_VERSION);
    memset(&g_settings,0,sizeof(g_settings));
    g_settings.volume=35; g_settings.brightness=60;
    strcpy(g_settings.timezone,"EET-2EEST,M3.5.0/3,M10.5.0/4");
    uint8_t random[16]; esp_fill_random(random,sizeof(random));
    for(int i=0;i<16;i++) snprintf(g_settings.token+i*2,3,"%02x",random[i]);
    settings_new_defaults(&g_settings);
    ESP_ERROR_CHECK(settings_save());
}
esp_err_t settings_save(void)
{
    nvs_handle_t nvs; esp_err_t err=nvs_open("radio",NVS_READWRITE,&nvs);
    if(err!=ESP_OK) return err;
    err=nvs_set_blob(nvs,"settings",&g_settings,sizeof(g_settings));
    if(err==ESP_OK) err=nvs_commit(nvs);
    nvs_close(nvs); return err;
}
