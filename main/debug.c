#include "runtime.h"
#include "config_nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
runtime_stats_t stats;
void record_max(uint32_t *p,uint32_t v) {
 uint32_t old=__atomic_load_n(p,__ATOMIC_RELAXED);
 if(v>old)__atomic_store_n(p,v,__ATOMIC_RELAXED);
}
void record_latency(bool notify,uint32_t value) {
#if CONFIG_TERA_DEBUG
 uint32_t *max=notify?&stats.notify_latency_max_us:&stats.uart_latency_max_us;
 uint32_t *sum=notify?&stats.notify_latency_sum:&stats.uart_latency_sum;
 uint32_t *count=notify?&stats.notify_latency_count:&stats.uart_latency_count;
 record_max(max,value);
 uint32_t s=__atomic_load_n(sum,__ATOMIC_RELAXED),n=__atomic_load_n(count,__ATOMIC_RELAXED);
 if(n<100000&&value<=UINT32_MAX-s){__atomic_store_n(sum,s+value,__ATOMIC_RELAXED);__atomic_store_n(count,n+1,__ATOMIC_RELAXED);}
#else
 (void)notify;(void)value;
#endif
}
void record_notify_stages(uint32_t queue_us,uint32_t api_us) {
#if CONFIG_TERA_DEBUG
 record_max(&stats.notify_queue_max,queue_us);record_max(&stats.notify_api_max,api_us);
 if(STAT_GET(notify_latency_count)<100000){
  uint32_t q=STAT_GET(notify_queue_sum),a=STAT_GET(notify_api_sum);
  if(queue_us<=UINT32_MAX-q)STAT_SET(notify_queue_sum,q+queue_us);
  if(api_us<=UINT32_MAX-a)STAT_SET(notify_api_sum,a+api_us);
 }
#else
 (void)queue_us;(void)api_us;
#endif
}
void probe(bool on) {
#if CONFIG_TERA_DEBUG && CONFIG_TERA_PROBE_GPIO >= 0
 gpio_set_level(CONFIG_TERA_PROBE_GPIO,on);
#else
 (void)on;
#endif
}
#if CONFIG_TERA_DEBUG
static StaticTask_t taskobj;static StackType_t stack[3072/sizeof(StackType_t)];
static void monitor(void *arg) {
 (void)arg;
 for(;;) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  ESP_LOGI("TERA","FW=%s IDF=%s baud=%lu source=%s score=%ld auto_search=%lu conn=%lu notify=%lu CI=%.2fms latency=%lu MTU=%lu PHY=1M adv=%s",
   TERA_VERSION,esp_get_idf_version(),(unsigned long)STAT_GET(current_baud),
   (const char *[]) {"DEFAULT","NVS","AUTO"}[STAT_GET(baud_source)],(long)STAT_GET(baud_score),(unsigned long)STAT_GET(baud_searching),
   (unsigned long)STAT_GET(connected),(unsigned long)STAT_GET(subscribed),STAT_GET(ci_units)*1.25,
   (unsigned long)STAT_GET(latency),(unsigned long)STAT_GET(mtu),STAT_GET(connected)?"OFF":STAT_GET(adv_fast)?"FAST":"SLOW");
  ESP_LOGI("TERA","U>B=%lu B>U=%lu parse=%lu/%lu UARTerr=%lu overflow RX/TX/BLE=%lu/%lu/%lu drop off/stale/notify=%lu/%lu/%lu max_us notify/enqueue=%lu/%lu heap=%lu",
   (unsigned long)STAT_GET(uart_to_ble),(unsigned long)STAT_GET(ble_to_uart),(unsigned long)STAT_GET(parse_uart),(unsigned long)STAT_GET(parse_ble),
   (unsigned long)STAT_GET(uart_errors),(unsigned long)STAT_GET(rx_overflow),(unsigned long)STAT_GET(tx_overflow),(unsigned long)STAT_GET(notify_overflow),
   (unsigned long)STAT_GET(unconnected_drop),(unsigned long)STAT_GET(stale_drop),(unsigned long)STAT_GET(notify_errors),
   (unsigned long)STAT_GET(notify_latency_max_us),(unsigned long)STAT_GET(uart_latency_max_us),(unsigned long)esp_get_free_heap_size());
  uint32_t a=STAT_GET(notify_latency_count),b=STAT_GET(uart_latency_count);
  ESP_LOGI("TERA","avg_us complete>notify_return=%lu gatt>tx_enqueue=%lu samples=%lu/%lu",
   (unsigned long)(a?STAT_GET(notify_latency_sum)/a:0),(unsigned long)(b?STAT_GET(uart_latency_sum)/b:0),(unsigned long)a,(unsigned long)b);
  ESP_LOGI("TERA","notify_stage avg_us request=%lu api=%lu max_us request=%lu api=%lu",
   (unsigned long)(a?STAT_GET(notify_queue_sum)/a:0),(unsigned long)(a?STAT_GET(notify_api_sum)/a:0),
   (unsigned long)STAT_GET(notify_queue_max),(unsigned long)STAT_GET(notify_api_max));
 }
}
#endif
void debug_init(void) {
#if CONFIG_TERA_DEBUG
 ESP_LOGI("TERA","boot name=%s",config_name());
#if CONFIG_TERA_PROBE_GPIO >= 0
 configASSERT(CONFIG_TERA_PROBE_GPIO!=CONFIG_TERA_RX_GPIO&&CONFIG_TERA_PROBE_GPIO!=CONFIG_TERA_TX_GPIO);
 gpio_config_t c={.pin_bit_mask=1ULL<<CONFIG_TERA_PROBE_GPIO,.mode=GPIO_MODE_OUTPUT};ESP_ERROR_CHECK(gpio_config(&c));
#endif
 configASSERT(xTaskCreateStatic(monitor,"debug",sizeof(stack),0,1,stack,&taskobj));
#endif
}
