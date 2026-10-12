#include "motion.h"
#include "motion_logic.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static i2c_master_dev_handle_t imu;
static portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
static motion_status_t cached;
void motion_snapshot(motion_status_t *out)
{
    portENTER_CRITICAL(&mutex);*out=cached;portEXIT_CRITICAL(&mutex);
}
static bool read_reg(uint8_t reg,uint8_t *data,size_t len)
{
    return i2c_master_transmit_receive(imu,&reg,1,data,len,100)==ESP_OK;
}
static bool write_reg(uint8_t reg,uint8_t value)
{
    uint8_t data[]={reg,value};return i2c_master_transmit(imu,data,2,100)==ESP_OK;
}
static void task(void *arg)
{
    (void)arg;motion_filter_t filter={0};uint32_t lifts=0;
    for(;;) {
        uint8_t ready=0,raw[6];motion_status_t next={.lifts=lifts};
        if(read_reg(0x2E,&ready,1)) {
            if((ready&1) && read_reg(0x35,raw,6)) {
                float a[3];for(int i=0;i<3;i++)a[i]=(int16_t)((uint16_t)raw[i*2]|((uint16_t)raw[i*2+1]<<8))/16384.f;
                if(motion_feed(&filter,a[0],a[1],a[2],esp_timer_get_time()))lifts++;
            }
            next.valid=filter.initialized;next.rotation=filter.rotation;next.lifts=lifts;
        }
        portENTER_CRITICAL(&mutex);cached=next;portEXIT_CRITICAL(&mutex);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
void motion_start(void)
{
    i2c_master_bus_handle_t bus=bsp_i2c_get_handle();if(!bus)return;
    for(int addr=0x6B;addr>=0x6A;addr--) {
        if(i2c_master_probe(bus,addr,100)!=ESP_OK)continue;
        const i2c_device_config_t config={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=addr,.scl_speed_hz=100000};
        if(i2c_master_bus_add_device(bus,&config,&imu)!=ESP_OK)continue;
        uint8_t id=0;if(read_reg(0,&id,1) && id==5)break;
        i2c_master_bus_rm_device(imu);imu=NULL;
    }
    if(!imu){ESP_LOGW("motion","QMI8658 unavailable; touch and keys still wake display");return;}
    // QMI8658 CTRL1: register auto-increment. Accel ±2g, low power 21Hz.
    // Gyroscope, FIFO and embedded algorithms remain disabled.
    if(!write_reg(0x08,0) || !write_reg(0x02,0x40) || !write_reg(0x03,0x0D) ||
       !write_reg(0x06,1) || !write_reg(0x08,1)) {
        ESP_LOGW("motion","QMI8658 configuration failed");i2c_master_bus_rm_device(imu);imu=NULL;return;
    }
    ESP_LOGI("motion","QMI8658: low-power accelerometer 21Hz; gyro disabled");
    configASSERT(xTaskCreate(task,"motion",3072,NULL,2,NULL)==pdPASS);
}
