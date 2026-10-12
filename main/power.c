#include "power_status.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static i2c_master_dev_handle_t pmu;
static portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
static power_status_t cached={.percent=-1};
void power_snapshot(power_status_t *status)
{
    portENTER_CRITICAL(&mutex);*status=cached;portEXIT_CRITICAL(&mutex);
}
static bool read_reg(uint8_t reg,uint8_t *data,size_t len)
{
    return i2c_master_transmit_receive(pmu,&reg,1,data,len,100)==ESP_OK;
}
static void update(void)
{
    uint8_t status[2],percent=255;
    power_status_t next={.percent=-1};
    if(read_reg(0x00,status,2)) {
        if(status[0]&8)read_reg(0xA4,&percent,1);
        next=power_decode(status[0],status[1],percent);
    }
    portENTER_CRITICAL(&mutex);cached=next;portEXIT_CRITICAL(&mutex);
}
static void power_task(void *arg)
{
    (void)arg;for(;;){update();vTaskDelay(pdMS_TO_TICKS(2000));}
}
void power_start(void)
{
    // Read-only telemetry: do not change charge limits, regulators or PMU settings.
    i2c_master_bus_handle_t bus=bsp_i2c_get_handle();if(!bus)return;
    if(i2c_master_probe(bus,0x34,100)!=ESP_OK){ESP_LOGW("power","AXP2101 not detected");return;}
    const i2c_device_config_t config={.dev_addr_length=I2C_ADDR_BIT_LEN_7,
        .device_address=0x34,.scl_speed_hz=100000};
    if(i2c_master_bus_add_device(bus,&config,&pmu)!=ESP_OK)return;
    uint8_t id=0;
    if(!read_reg(0x03,&id,1) || id!=0x4A) {
        ESP_LOGW("power","Unexpected PMU ID: 0x%02x",id);
        i2c_master_bus_rm_device(pmu);pmu=NULL;return;
    }
    update();ESP_LOGI("power","AXP2101 read-only telemetry enabled");
    configASSERT(xTaskCreate(power_task,"power",3072,NULL,2,NULL)==pdPASS);
}
