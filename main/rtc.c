#include "app.h"
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"
static i2c_master_dev_handle_t rtc;
static unsigned bcd(uint8_t v) {return (v>>4)*10+(v&15);}
static uint8_t encode(unsigned v) {return ((v/10)<<4)|(v%10);}
static bool leap(int y) {return y%4==0 && (y%100!=0 || y%400==0);}
static void restore(void)
{
    uint8_t reg=4,v[7];if(i2c_master_transmit_receive(rtc,&reg,1,v,7,200)!=ESP_OK || (v[0]&0x80))return;
    int sec=bcd(v[0]&0x7f),min=bcd(v[1]&0x7f),hour=bcd(v[2]&0x3f),day=bcd(v[3]&0x3f),month=bcd(v[5]&0x1f),year=2000+bcd(v[6]);
    if(sec>59||min>59||hour>23||day<1||day>31||month<1||month>12||year<2024||year>2099)return;
    static const int mdays[]={31,28,31,30,31,30,31,31,30,31,30,31};int days=0;
    for(int y=1970;y<year;y++)days+=365+leap(y);
    for(int m=1;m<month;m++)days+=mdays[m-1]+(m==2&&leap(year));
    days+=day-1;struct timeval tv={.tv_sec=(time_t)days*86400+hour*3600+min*60+sec};settimeofday(&tv,NULL);
}
void rtc_sync_from_system(void)
{
    if(!rtc)return;
    time_t now=time(NULL);if(now<1704067200)return;struct tm utc;gmtime_r(&now,&utc);
    if(utc.tm_year<100||utc.tm_year>199)return;
    uint8_t v[]={4,encode(utc.tm_sec),encode(utc.tm_min),encode(utc.tm_hour),encode(utc.tm_mday),encode(utc.tm_wday),encode(utc.tm_mon+1),encode(utc.tm_year-100)};
    i2c_master_transmit(rtc,v,sizeof(v),200);
}
static void rtc_task(void *arg) {(void)arg;for(;;){vTaskDelay(pdMS_TO_TICKS(60000));rtc_sync_from_system();}}
void rtc_start(void)
{
    i2c_device_config_t config={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x51,.scl_speed_hz=100000};
    i2c_master_bus_handle_t bus=bsp_i2c_get_handle();if(!bus)return;
    if(i2c_master_probe(bus,0x51,200)!=ESP_OK)return;
    if(i2c_master_bus_add_device(bus,&config,&rtc)!=ESP_OK)return;
    restore();configASSERT(xTaskCreate(rtc_task,"rtc",3072,NULL,2,NULL)==pdPASS);
}
