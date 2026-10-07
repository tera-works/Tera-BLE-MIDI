#include "uart_midi.h"
#include "runtime.h"
#include "baud_detect.h"
#include "ble_codec.h"
#include "ble_midi.h"
#include "config_nvs.h"
#include "name_command.h"
#include "power_manager.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "hal/uart_ll.h"
#include "soc/interrupts.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#define PORT UART_NUM_1
#define RXMASK (UART_INTR_RXFIFO_FULL|UART_INTR_RXFIFO_TOUT|UART_INTR_FRAM_ERR|UART_INTR_PARITY_ERR|UART_INTR_RXFIFO_OVF|UART_INTR_BRK_DET)
static uart_dev_t *const hw=UART_LL_GET_HW(PORT);
static uint8_t rx[UART_RX_RING_SIZE],tx[UART_TX_RING_SIZE];
static unsigned rh,rt,th,tt;
static unsigned rx_faults;
static bool reset_rx;
static bool rx_loss;
static bool tx_eox_pending;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t task;
static StaticTask_t taskobj;static StackType_t stack[4096/sizeof(StackType_t)];
static midi_parser_t parser;static ble_encoder_t encoder;static baud_detect_t detector;
static bool skip_sysex;
static name_filter_t name_filter;
static ble_submit_context_t submit_context;
static void IRAM_ATTR fill_tx(void) {
 unsigned space=uart_ll_get_txfifo_len(hw); /* LL API returns writable space. */
 while(space&&tt!=th) {
  unsigned n=(th>=tt)?th-tt:UART_TX_RING_SIZE-tt;
  if(n>space)n=space;
  uart_ll_write_txfifo(hw,tx+tt,n);tt=(tt+n)%UART_TX_RING_SIZE;space-=n;
 }
 if(space&&tt==th&&tx_eox_pending) {
  const uint8_t end=0xf7;uart_ll_write_txfifo(hw,&end,1);tx_eox_pending=false;
 }
 if(tt!=th||tx_eox_pending)uart_ll_ena_intr_mask(hw,UART_INTR_TXFIFO_EMPTY);
 else uart_ll_disable_intr_mask(hw,UART_INTR_TXFIFO_EMPTY);
}
static void IRAM_ATTR interrupt(void *arg) {
 (void)arg;BaseType_t woke=pdFALSE;uint32_t s=uart_ll_get_intsts_mask(hw);
 portENTER_CRITICAL_ISR(&mux);
 uart_ll_clr_intsts_mask(hw,s);
 if(s&(UART_INTR_FRAM_ERR|UART_INTR_PARITY_ERR|UART_INTR_RXFIFO_OVF|UART_INTR_BRK_DET)) {
  if(s&(UART_INTR_FRAM_ERR|UART_INTR_PARITY_ERR|UART_INTR_BRK_DET))rx_faults++;
  reset_rx=rx_loss=true;rh=rt=0;
  uart_ll_rxfifo_rst(hw);
  if(s&UART_INTR_RXFIFO_OVF)STAT_INC(rx_overflow);
 } else if(s&(UART_INTR_RXFIFO_FULL|UART_INTR_RXFIFO_TOUT)) {
  unsigned n=uart_ll_get_rxfifo_len(hw);
  for(unsigned i=0;i<n;i++) {
   uint8_t b;uart_ll_read_rxfifo(hw,&b,1);
   unsigned next=(rh+1)%UART_RX_RING_SIZE;
   if(next==rt){STAT_INC(rx_overflow);reset_rx=rx_loss=true;rh=rt=0;uart_ll_rxfifo_rst(hw);break;}
   rx[rh]=b;rh=next;
  }
 }
 if(s&UART_INTR_TXFIFO_EMPTY)fill_tx();
 portEXIT_CRITICAL_ISR(&mux);
 if(s&RXMASK)vTaskNotifyGiveFromISR(task,&woke);
 if(woke)portYIELD_FROM_ISR();
}
bool uart_midi_write(const uint8_t *d,size_t n) {
 if(!n)return true;
 bool ok=true;portENTER_CRITICAL(&mux);
 unsigned free=(tt+UART_TX_RING_SIZE-th-1)%UART_TX_RING_SIZE;
 if(n+(tx_eox_pending?1:0)>free){ok=false;STAT_INC(tx_overflow);}
 else {
  if(tx_eox_pending){tx[th]=0xf7;th=(th+1)%UART_TX_RING_SIZE;tx_eox_pending=false;}
  for(size_t i=0;i<n;i++){tx[th]=d[i];th=(th+1)%UART_TX_RING_SIZE;}
  fill_tx();
 }
 portEXIT_CRITICAL(&mux);return ok;
}
void uart_midi_abort_sysex(void) {
 portENTER_CRITICAL(&mux);tx_eox_pending=true;fill_tx();portEXIT_CRITICAL(&mux);
}
void uart_midi_session(void) {
 portENTER_CRITICAL(&mux);rh=rt=0;reset_rx=true;uart_ll_rxfifo_rst(hw);portEXIT_CRITICAL(&mux);
 if(task)xTaskNotifyGive(task);
}
static void midi_out(void *ctx,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 (void)ctx;
 if(!ble_midi_ready()||detector.searching){STAT_INC(unconnected_drop);return;}
 if(kind==MIDI_SYSEX) {
  if(d[0]==0xf0)skip_sysex=false;
  if(skip_sysex)return;
 }
 probe(true);int64_t start=esp_timer_get_time();
 submit_context.start_us=(uint32_t)start;
 /* Completion time keeps outgoing timestamps monotonic when realtime interrupts a message. */
 ts=(uint16_t)(start/1000)&8191;
 encoder.limit=ble_midi_payload();
 bool ok=ble_encode(&encoder,d,n,ts,kind);
 if(!ok&&kind==MIDI_SYSEX)skip_sysex=true;
 if(ok&&(kind!=MIDI_SYSEX||d[n-1]==0xf7))STAT_INC(uart_to_ble);
 probe(false);
}
static void close_transmitted_sysex(void) {
 if(encoder.sysex&&ble_midi_ready()) {
  const uint8_t end=0xf7;submit_context.start_us=(uint32_t)esp_timer_get_time();
  (void)ble_encode(&encoder,&end,1,(uint16_t)(esp_timer_get_time()/1000)&8191,MIDI_SYSEX);
 }
}
static void filtered_midi(void *ctx,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 (void)ctx;
 if(parser.sysex_aborted&&name_filter_abort(&name_filter))return;
 name_filter_feed(&name_filter,d,n,ts,kind);
}
static void change_baud(void) {
 close_transmitted_sysex();
 /* Search data is discarded. Do not mix old UART TX bytes with a new baud. */
 portENTER_CRITICAL(&mux);
 uart_ll_disable_intr_mask(hw,0xffffffff);rh=rt=th=tt=0;tx_eox_pending=false;
 uart_ll_rxfifo_rst(hw);uart_ll_txfifo_rst(hw);portEXIT_CRITICAL(&mux);
 ESP_ERROR_CHECK(uart_set_baudrate(PORT,detector.baud));
 midi_reset(&parser);name_filter_reset(&name_filter);encoder.sysex=false;skip_sysex=true;
 portENTER_CRITICAL(&mux);uart_ll_clr_intsts_mask(hw,0xffffffff);uart_ll_ena_intr_mask(hw,RXMASK);portEXIT_CRITICAL(&mux);
 STAT_SET(current_baud,detector.baud);
 STAT_SET(baud_source,SOURCE_AUTO);
}
static void receiver(void *arg) {
 (void)arg;uint8_t buf[128];
 for(;;) {
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);power_enter();
  for(;;) {
   unsigned errors,n=0;bool reset,loss;
   portENTER_CRITICAL(&mux);errors=rx_faults;rx_faults=0;reset=reset_rx;reset_rx=false;
   loss=rx_loss;rx_loss=false;
   submit_context.generation=ble_midi_generation();
   while(n<sizeof(buf)&&rt!=rh){buf[n++]=rx[rt];rt=(rt+1)%UART_RX_RING_SIZE;}
   portEXIT_CRITICAL(&mux);
   if(reset){if(loss)close_transmitted_sysex();midi_reset(&parser);name_filter_reset(&name_filter);encoder.sysex=false;skip_sysex=true;}
   if(!n&&!errors)break;
   config_activity();__atomic_fetch_add(&stats.uart_errors,errors,__ATOMIC_RELAXED);
   bool was_confirmed=detector.confirmed,was_searching=detector.searching;
   bool changed=baud_observe(&detector,buf,n,errors,(uint32_t)(esp_timer_get_time()/1000));
   STAT_SET(baud_score,detector.last_score);
   STAT_SET(baud_searching,detector.searching);
   if(changed){change_baud();continue;}
   if(!was_confirmed&&detector.confirmed) {
    if(STAT_GET(current_baud)!=detector.baud)STAT_SET(current_baud,detector.baud);
    if(was_searching)STAT_SET(baud_source,SOURCE_AUTO);
    config_save_later(detector.baud);
   }
   if(detector.searching)continue;
   uint16_t ts=(uint16_t)(esp_timer_get_time()/1000)&8191;
   for(unsigned i=0;i<n;i++)midi_feed(&parser,buf[i],ts);
   midi_flush(&parser);STAT_SET(parse_uart,parser.invalid);
  }
  power_exit();
 }
}
void uart_midi_init(uint32_t baud) {
 configASSERT(CONFIG_TERA_RX_GPIO!=CONFIG_TERA_TX_GPIO);
 configASSERT(GPIO_IS_VALID_GPIO(CONFIG_TERA_RX_GPIO)&&GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_TERA_TX_GPIO));
 /* UART1 keeps ROM boot output and debug UART0 away from the MIDI wires. */
 uart_config_t c={.baud_rate=baud,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
  .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_REF_TICK};
 ESP_ERROR_CHECK(uart_param_config(PORT,&c));
 ESP_ERROR_CHECK(uart_set_pin(PORT,CONFIG_TERA_TX_GPIO,CONFIG_TERA_RX_GPIO,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
 if(CONFIG_TERA_RX_GPIO<34)ESP_ERROR_CHECK(gpio_set_pull_mode(CONFIG_TERA_RX_GPIO,GPIO_PULLUP_ONLY));
 uart_ll_disable_intr_mask(hw,0xffffffff);uart_ll_clr_intsts_mask(hw,0xffffffff);
 uart_ll_rxfifo_rst(hw);uart_ll_txfifo_rst(hw);
 uart_ll_set_rxfifo_full_thr(hw,1);uart_ll_set_txfifo_empty_thr(hw,32);uart_ll_set_rx_tout(hw,2);
 name_filter_init(&name_filter,midi_out,0,config_name_later);
 midi_init(&parser,filtered_midi,0);ble_encoder_init(&encoder,ble_midi_submit,&submit_context);baud_init(&detector,baud);
 skip_sysex=true;
 task=xTaskCreateStatic(receiver,"uart_midi",sizeof(stack),0,20,stack,&taskobj);configASSERT(task);
 intr_handle_t handle;
 ESP_ERROR_CHECK(esp_intr_alloc(ETS_UART1_INTR_SOURCE,ESP_INTR_FLAG_IRAM|ESP_INTR_FLAG_LEVEL1,interrupt,0,&handle));
 uart_ll_ena_intr_mask(hw,RXMASK);
}
