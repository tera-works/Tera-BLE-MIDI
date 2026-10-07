#include "runtime.h"
#include "config_nvs.h"
#include "power_manager.h"
#include "uart_midi.h"
#include "ble_midi.h"
#include "status_led.h"
void app_main(void) {
 uint32_t source;uint32_t baud=config_load(&source);
 STAT_SET(current_baud,baud);STAT_SET(baud_source,source);
 power_init();status_led_init();uart_midi_init(baud);ble_midi_init();debug_init();
}
