#include "app.h"
#include "power_status.h"
#include "motion.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
void app_main(void)
{
    settings_init();app_init();
    if(!display_start()) {ESP_LOGE("radio","Display initialization failed; web control remains available");}
    else ui_start();
    power_start();motion_start();rtc_start();network_start();web_start();audio_start();
}
