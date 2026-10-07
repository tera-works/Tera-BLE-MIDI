#include "power_manager.h"
#include "esp_pm.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "sdkconfig.h"
static esp_pm_lock_handle_t cpu;
void power_init(void) {
 esp_pm_config_t c={.max_freq_mhz=240,.min_freq_mhz=40,.light_sleep_enable=false};
 ESP_ERROR_CHECK(esp_pm_configure(&c));
 ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"midi",&cpu));
}
void power_connected(bool c) {
 /* VHCI sends synchronously request a controller wake-up. Avoid that wait
  * on the first MIDI event too; retain modem sleep while advertising. */
 esp_err_t rc=c?esp_bt_sleep_disable():esp_bt_sleep_enable();
#if CONFIG_TERA_DEBUG
 ESP_LOGI("TERA","controller_sleep=%s rc=%s",c?"OFF_CONNECTED":"ON_ADVERTISING",esp_err_to_name(rc));
#else
 (void)rc;
#endif
}
void power_enter(void){esp_pm_lock_acquire(cpu);}
void power_exit(void){esp_pm_lock_release(cpu);}
