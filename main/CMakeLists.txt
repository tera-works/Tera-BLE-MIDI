idf_component_register(SRCS "main.c" "midi_parser.c" "ble_codec.c" "baud_detect.c"
 "ble_midi.c" "uart_midi.c" "config_nvs.c" "name_command.c" "power_manager.c" "debug.c" "status_led.c"
 INCLUDE_DIRS "." REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio esp_timer esp_pm esp_system esp_hal_uart)
