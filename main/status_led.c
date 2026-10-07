#include "status_led.h"
#include "runtime.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_TERA_LED_ENABLED
/* This observer runs below UART (20) and NimBLE (21). No LED calls, delays,
 * task notifications, or GPIO writes are added to the MIDI critical path.
 * Traffic hooks only increment relaxed atomic counters after successful I/O.
 * A burst is coalesced into one indication: never replay old notes on the LED. */
static StaticTask_t task_object;
static StackType_t stack[2048 / sizeof(StackType_t)];
static void set_led(bool on) {
#if CONFIG_TERA_LED_ACTIVE_LOW
 gpio_set_level(CONFIG_TERA_LED_GPIO,!on);
#else
 gpio_set_level(CONFIG_TERA_LED_GPIO,on);
#endif
}
static uint32_t now_ms(void){return (uint32_t)(esp_timer_get_time()/1000);}
static void display(void *arg) {
 (void)arg;
 uint32_t seen_tx=STAT_GET(led_notify),seen_rx=STAT_GET(led_uart);
 uint32_t start=now_ms();
 unsigned previous=0,pending=0,pulses=0;
 bool showing=false,cooldown=false;
 for(;;){
  uint32_t now=now_ms(),tx=STAT_GET(led_notify),rx=STAT_GET(led_uart);
  unsigned state=!STAT_GET(connected)?0:STAT_GET(subscribed)?2:1;
  if(state!=previous){
   previous=state;start=now;pending=pulses=0;showing=cooldown=false;
  }
  if(state==2){
   if(tx!=seen_tx)pending|=1; /* UART RX -> BLE Notify request accepted. */
   if(rx!=seen_rx)pending|=2; /* BLE MIDI -> UART TX queue accepted. */
   if(showing && (uint32_t)(now-start)>=pulses*2u*LED_ACTIVITY_EDGE_MS){
    showing=false;cooldown=true;start=now;
   }
   if(cooldown && (uint32_t)(now-start)>=LED_ACTIVITY_GAP_MS)cooldown=false;
   if(!showing && !cooldown && pending){
    pulses=pending==3?3:pending==2?2:1;
    pending=0;start=now;showing=true;
   }
   /* Connected idle is solid ON. Traffic is indicated by dark pulses so
    * connection remains distinguishable from waiting even at high traffic. */
   set_led(!showing || ((now-start)/LED_ACTIVITY_EDGE_MS)%2u!=0);
  }else{
   pending=0;showing=cooldown=false;
   uint32_t period=state==0?LED_WAIT_PERIOD_MS:LED_LINK_PERIOD_MS;
   uint32_t on=state==0?LED_WAIT_ON_MS:LED_LINK_ON_MS;
   set_led((uint32_t)(now-start)%period<on);
  }
  seen_tx=tx;seen_rx=rx;
  vTaskDelay(pdMS_TO_TICKS(showing?25:100));
 }
}
#endif
void status_led_init(void){
#if CONFIG_TERA_LED_ENABLED
 configASSERT(GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_TERA_LED_GPIO));
 configASSERT(CONFIG_TERA_LED_GPIO<6 || CONFIG_TERA_LED_GPIO>11);
 configASSERT(CONFIG_TERA_LED_GPIO!=CONFIG_TERA_RX_GPIO && CONFIG_TERA_LED_GPIO!=CONFIG_TERA_TX_GPIO);
 configASSERT(CONFIG_TERA_LED_GPIO!=1 && CONFIG_TERA_LED_GPIO!=3);
#if CONFIG_TERA_DEBUG && CONFIG_TERA_PROBE_GPIO >= 0
 configASSERT(CONFIG_TERA_LED_GPIO!=CONFIG_TERA_PROBE_GPIO);
#endif
 /* Set the latch before enabling output to avoid an active-low startup flash. */
 set_led(false);
 gpio_config_t c={.pin_bit_mask=1ULL<<CONFIG_TERA_LED_GPIO,.mode=GPIO_MODE_OUTPUT};
 ESP_ERROR_CHECK(gpio_config(&c));
 configASSERT(xTaskCreateStatic(display,"status_led",sizeof(stack),0,1,stack,&task_object));
#endif
}
