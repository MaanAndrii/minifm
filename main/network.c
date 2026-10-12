#include "app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_mac.h"
static esp_netif_t *sta,*ap;
static atomic_bool connected,ap_started;
static bool sntp_started;
static void events(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg;(void)data;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START)esp_wifi_connect();
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {atomic_store(&connected,false);esp_wifi_connect();}
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        atomic_store(&connected,true);if(!sntp_started) {esp_netif_sntp_start();sntp_started=true;}
    }
}
static void start_ap(void)
{
    if(atomic_load(&ap_started))return;
    settings_t *cfg=malloc(sizeof(*cfg));if(!cfg)return;app_snapshot(cfg,NULL);
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);
    wifi_config_t wifi={0};snprintf((char *)wifi.ap.ssid,sizeof(wifi.ap.ssid),"AMOLED-Radio-%02X%02X",mac[4],mac[5]);
    memcpy(wifi.ap.password,cfg->token,16);wifi.ap.password[16]=0;
    wifi.ap.ssid_len=strlen((char *)wifi.ap.ssid);wifi.ap.channel=1;wifi.ap.max_connection=4;wifi.ap.authmode=WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(cfg->ssid[0]?WIFI_MODE_APSTA:WIFI_MODE_AP);free(cfg);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&wifi));atomic_store(&ap_started,true);
}
static void fallback(void *arg)
{
    (void)arg;vTaskDelay(pdMS_TO_TICKS(30000));
    for(;;) {if(!atomic_load(&connected))start_ap();vTaskDelay(pdMS_TO_TICKS(30000));}
}
void network_status(char *out,size_t size)
{
    esp_netif_ip_info_t info;
    if(atomic_load(&connected) && esp_netif_get_ip_info(sta,&info)==ESP_OK)snprintf(out,size,"http://" IPSTR,IP2STR(&info.ip));
    else snprintf(out,size,atomic_load(&ap_started)?"AP: http://192.168.4.1":"Connecting Wi-Fi...");
}
int network_indicator(int *rssi)
{
    *rssi=-127;
    if(atomic_load(&connected)) {
        wifi_ap_record_t record;
        if(esp_wifi_sta_get_ap_info(&record)==ESP_OK){*rssi=record.rssi;return 1;}
    }
    return atomic_load(&ap_started)?2:0;
}
void network_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta=esp_netif_create_default_wifi_sta();ap=esp_netif_create_default_wifi_ap();(void)ap;
    esp_netif_set_hostname(sta,"amoled-radio");
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,events,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,events,NULL));
    esp_sntp_config_t sntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");sntp.start=false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    if(g_settings.ssid[0]) {
        wifi_config_t config={0};strcpy((char *)config.sta.ssid,g_settings.ssid);strcpy((char *)config.sta.password,g_settings.password);
        config.sta.pmf_cfg.capable=true;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&config));
    } else {ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));start_ap();}
    ESP_ERROR_CHECK(esp_wifi_start());esp_wifi_set_ps(WIFI_PS_NONE);
    configASSERT(xTaskCreate(fallback,"wifi-fallback",4096,NULL,3,NULL)==pdPASS);
}
