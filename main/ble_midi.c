#include "ble_midi.h"
#include "runtime.h"
#include "ble_codec.h"
#include "uart_midi.h"
#include "power_manager.h"
#include "config_nvs.h"
#include "name_command.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
/* UUID byte order is little endian in NimBLE. */
static const ble_uuid128_t service=BLE_UUID128_INIT(0x00,0xc7,0xc4,0x4e,0xe3,0x6c,0x51,0xa7,0x33,0x4b,0xe8,0xed,0x5a,0x0e,0xb8,0x03);
static const ble_uuid128_t characteristic=BLE_UUID128_INIT(0xf3,0x6b,0x10,0x9d,0x66,0xf2,0xa9,0xa1,0x12,0x41,0x68,0x38,0xdb,0xe5,0x72,0x77);
static uint16_t value_handle,connection=BLE_HS_CONN_HANDLE_NONE;
static uint8_t addr_type;
static struct ble_npl_event send_event;
static struct ble_npl_callout adv_timer,param_timer;
static ble_decoder_t decoder;
static name_filter_t name_filter;
static char pending_name[MIDI_NAME_MAX+1];
static bool pending_rename;
typedef struct {uint8_t data[BLE_PACKET_MAX];uint16_t n;uint32_t time_us,epoch;} queued_t;
static queued_t queue[NOTIFY_QUEUE_SIZE];static unsigned head,tail;static uint32_t epoch;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static bool fifo_failed;
static bool fifo_failed_sysex;
static bool remote_sysex,recover_sysex;
static uint32_t queue_lost;
uint32_t ble_midi_generation(void){return __atomic_load_n(&epoch,__ATOMIC_RELAXED);}
static int gap(struct ble_gap_event *,void *);
static void restart_advertising(bool fast);
bool ble_midi_ready(void){return STAT_GET(subscribed)!=0;}
uint16_t ble_midi_payload(void) {
 uint32_t mtu=STAT_GET(mtu);return mtu>=23&&mtu<=185?(uint16_t)(mtu-3):20;
}
static void clear_session(void) {
 STAT_SET(subscribed,0);
 portENTER_CRITICAL(&mux);head=tail=0;__atomic_fetch_add(&epoch,1,__ATOMIC_RELAXED);portEXIT_CRITICAL(&mux);
 midi_reset(&decoder.midi);name_filter_reset(&name_filter);uart_midi_session();
 remote_sysex=recover_sysex=false;__atomic_store_n(&queue_lost,0,__ATOMIC_RELAXED);
}
bool ble_midi_submit(void *ctx,const uint8_t *data,size_t n) {
 const ble_submit_context_t *info=ctx;
 if(!ble_midi_ready()||n<2||n>BLE_PACKET_MAX)return false;
 bool ok=true;
 portENTER_CRITICAL(&mux);
 if(!ble_midi_ready()||(info&&info->generation!=epoch)){portEXIT_CRITICAL(&mux);return false;}
 unsigned next=(head+1)%NOTIFY_QUEUE_SIZE;
 if(next==tail){STAT_INC(notify_overflow);__atomic_store_n(&queue_lost,1,__ATOMIC_RELAXED);ok=false;}
 else {
  queued_t *q=&queue[head];q->n=n;q->time_us=info?info->start_us:(uint32_t)esp_timer_get_time();q->epoch=epoch;
  memcpy(q->data,data,n);head=next;
 }
 portEXIT_CRITICAL(&mux);
 ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&send_event);
 return ok;
}
static bool close_remote_sysex(uint16_t ts) {
 if(!remote_sysex){recover_sysex=false;return true;}
 uint8_t end[]={0x80|((ts>>7)&63),0x80|(ts&127),0xf7};
 struct os_mbuf *om=ble_hs_mbuf_from_flat(end,sizeof(end));
 if(!om)return false;
 if(ble_gatts_notify_custom(connection,value_handle,om))return false;
 remote_sysex=recover_sysex=false;return true;
}
static void send_packets(struct ble_npl_event *ev) {
 (void)ev;queued_t q;unsigned budget=NOTIFY_QUEUE_SIZE;
 power_enter();
 while(budget--) {
  portENTER_CRITICAL(&mux);
  if(tail==head){portEXIT_CRITICAL(&mux);break;}
  q=queue[tail];tail=(tail+1)%NOTIFY_QUEUE_SIZE;
  portEXIT_CRITICAL(&mux);
  if(q.epoch!=epoch||!ble_midi_ready())continue;
  if((uint32_t)((uint32_t)esp_timer_get_time()-q.time_us)>NOTIFY_MAX_AGE_US){STAT_INC(stale_drop);recover_sysex=true;uart_midi_session();continue;}
  uint16_t next_ts=(q.data[1]&0x80)?((q.data[0]&63)<<7)|(q.data[1]&127):(uint16_t)(q.time_us/1000)&8191;
  if(recover_sysex&&!close_remote_sysex(next_ts)){STAT_INC(notify_errors);continue;}
  bool continuation=q.data[1]<0x80||(q.n>=3&&q.data[2]==0xf7);
  if(continuation&&!remote_sysex){STAT_INC(stale_drop);continue;}
  /* NimBLE mbufs come from preallocated MSYS pools, not per-message malloc. */
  struct os_mbuf *om=ble_hs_mbuf_from_flat(q.data,q.n);
  if(!om){STAT_INC(notify_errors);recover_sysex=true;uart_midi_session();continue;}
  #if CONFIG_TERA_DEBUG
  uint32_t request_start=(uint32_t)esp_timer_get_time();
  #endif
  int rc=ble_gatts_notify_custom(connection,value_handle,om); /* consumes mbuf even on failure */
  #if CONFIG_TERA_DEBUG
  uint32_t api_return=(uint32_t)esp_timer_get_time();
  record_notify_stages(request_start-q.time_us,api_return-request_start);
  record_latency(true,api_return-q.time_us);
  #endif
  if(rc){STAT_INC(notify_errors);recover_sysex=true;uart_midi_session();}
  else {
   STAT_INC(led_notify);
   if(q.n>=3&&(q.data[1]&0x80)&&q.data[2]==0xf0)remote_sysex=true;
   if(q.data[q.n-1]==0xf7)remote_sysex=false;
  }
 }
 if(__atomic_exchange_n(&queue_lost,0,__ATOMIC_RELAXED))recover_sysex=true;
 if(recover_sysex)(void)close_remote_sysex((uint16_t)(esp_timer_get_time()/1000)&8191);
 power_exit();
}
static void to_uart(void *ctx,const uint8_t *data,size_t n,uint16_t ts,uint8_t kind) {
 (void)ctx;(void)ts;
 if(!fifo_failed){
  if(!uart_midi_write(data,n)){fifo_failed=true;fifo_failed_sysex=kind==MIDI_SYSEX||decoder.midi.in_sysex;}
  else STAT_INC(led_uart);
 }
 if(!fifo_failed&&(kind!=MIDI_SYSEX||data[n-1]==0xf7))STAT_INC(ble_to_uart);
}
static void filtered_midi(void *ctx,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 (void)ctx;
 if(decoder.midi.sysex_aborted&&name_filter_abort(&name_filter))return;
 name_filter_feed(&name_filter,d,n,ts,kind);
}
static void receive_name(const char *name){strcpy(pending_name,name);pending_rename=true;}
static int access_midi(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *c,void *arg) {
 (void)attr;(void)arg;
 if(c->op==BLE_GATT_ACCESS_OP_READ_CHR)return 0; /* No stale MIDI readable value. */
 if(c->op!=BLE_GATT_ACCESS_OP_WRITE_CHR||conn!=connection)return BLE_ATT_ERR_UNLIKELY;
 int64_t start=esp_timer_get_time();
 uint16_t n=OS_MBUF_PKTLEN(c->om);uint8_t buf[BLE_PACKET_MAX];
 if(n>sizeof(buf)||n<2||os_mbuf_copydata(c->om,0,n,buf)) {
  fifo_failed=fifo_failed_sysex=false;(void)ble_decode(&decoder,0,0);
  if(fifo_failed_sysex)uart_midi_abort_sysex();
  name_filter_reset(&name_filter);STAT_INC(parse_ble);
  return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
 }
 power_enter();config_activity();probe(true);fifo_failed=fifo_failed_sysex=false;pending_rename=false;
 bool valid=ble_decode(&decoder,buf,n);
 if(!valid||fifo_failed){STAT_INC(parse_ble);if(fifo_failed_sysex)uart_midi_abort_sysex();midi_reset(&decoder.midi);name_filter_reset(&name_filter);}
 else if(pending_rename)config_name_later(pending_name);
 record_latency(false,(uint32_t)(esp_timer_get_time()-start));probe(false);power_exit();
 return valid&&!fifo_failed?0:BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[]={
 {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service.u,
  .characteristics=(struct ble_gatt_chr_def[]){
   {.uuid=&characteristic.u,.access_cb=access_midi,.val_handle=&value_handle,
    .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_NOTIFY},
   {0}}}, {0}
};
static void slow_advertising(struct ble_npl_event *ev) {(void)ev;if(connection==BLE_HS_CONN_HANDLE_NONE)restart_advertising(false);}
static void request_parameters(struct ble_npl_event *ev) {
 (void)ev;if(connection==BLE_HS_CONN_HANDLE_NONE)return;
 /* A fallback must not lengthen an interval which already meets the target. */
 if(ev){
  struct ble_gap_conn_desc current;
  if(ble_gap_conn_find(connection,&current)==0&&current.conn_itvl==6&&current.conn_latency==0)return;
 }
 struct ble_gap_upd_params p={.itvl_min=6,.itvl_max=ev?12:6,.latency=0,.supervision_timeout=100};
 (void)ble_gap_update_params(connection,&p);
}
static void update_connection(void) {
 struct ble_gap_conn_desc d;
 if(ble_gap_conn_find(connection,&d))return;
 STAT_SET(ci_units,d.conn_itvl);STAT_SET(latency,d.conn_latency);
}
static void restart_advertising(bool fast) {
 if(connection!=BLE_HS_CONN_HANDLE_NONE)return;
 if(ble_gap_adv_active())ble_gap_adv_stop();
 struct ble_hs_adv_fields f={0};
 f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
 f.uuids128=(ble_uuid128_t *)&service;f.num_uuids128=1;f.uuids128_is_complete=1;
 if(ble_gap_adv_set_fields(&f))return;
 struct ble_hs_adv_fields rsp={0};rsp.name=(uint8_t *)config_name();
 rsp.name_len=strlen(config_name());rsp.name_is_complete=1;
 if(ble_gap_adv_rsp_set_fields(&rsp))return;
 struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
  .itvl_min=fast?FAST_ADV_UNITS:SLOW_ADV_UNITS,.itvl_max=fast?FAST_ADV_UNITS:SLOW_ADV_UNITS};
 int rc=ble_gap_adv_start(addr_type,0,BLE_HS_FOREVER,&p,gap,0);
 STAT_SET(adv_fast,fast);
 if(!rc&&fast)ble_npl_callout_reset(&adv_timer,ble_npl_time_ms_to_ticks32(FAST_ADV_SECONDS*1000));
}
static int gap(struct ble_gap_event *e,void *arg) {
 (void)arg;
 switch(e->type) {
 case BLE_GAP_EVENT_CONNECT:
  if(e->connect.status){restart_advertising(true);break;}
  connection=e->connect.conn_handle;clear_session();STAT_SET(connected,1);STAT_SET(mtu,23);
  config_link_changed();
  ble_npl_callout_stop(&adv_timer);power_connected(true);update_connection();
  request_parameters(0);
  /* One deferred request handles hosts which reject updates during discovery. */
  ble_npl_callout_reset(&param_timer,ble_npl_time_ms_to_ticks32(1000));break;
 case BLE_GAP_EVENT_DISCONNECT:
  connection=BLE_HS_CONN_HANDLE_NONE;clear_session();STAT_SET(connected,0);STAT_SET(mtu,23);
  config_link_changed();
  ble_npl_callout_stop(&param_timer);power_connected(false);restart_advertising(true);break;
 case BLE_GAP_EVENT_SUBSCRIBE:
  if(e->subscribe.attr_handle==value_handle) {
   clear_session();STAT_SET(subscribed,e->subscribe.cur_notify);
  }break;
 case BLE_GAP_EVENT_MTU:
  STAT_SET(mtu,e->mtu.value);break;
 case BLE_GAP_EVENT_CONN_UPDATE:update_connection();break;
 case BLE_GAP_EVENT_ADV_COMPLETE:restart_advertising(true);break;
 default:break;
 }
 return 0;
}
static void sync_host(void) {
 int rc=ble_hs_util_ensure_addr(0);configASSERT(rc==0);
 rc=ble_hs_id_infer_auto(0,&addr_type);configASSERT(rc==0);restart_advertising(true);
}
static void host(void *arg){(void)arg;nimble_port_run();nimble_port_freertos_deinit();}
void ble_midi_init(void) {
 configASSERT(strlen(CONFIG_TERA_NAME)>0&&strlen(CONFIG_TERA_NAME)<=29);
 ESP_ERROR_CHECK(nimble_port_init());
 ble_svc_gap_init();ble_svc_gatt_init();
 configASSERT(ble_svc_gap_device_name_set(config_name())==0);
 ble_hs_cfg.sync_cb=sync_host;
 /* Just Works encryption supported on request; no saved host or bonding. */
 ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_sc=1;
 configASSERT(ble_gatts_count_cfg(services)==0);configASSERT(ble_gatts_add_svcs(services)==0);
 name_filter_init(&name_filter,to_uart,0,receive_name);
 ble_decoder_init(&decoder,filtered_midi,0);
 ble_npl_event_init(&send_event,send_packets,0);
 configASSERT(ble_npl_callout_init(&adv_timer,nimble_port_get_dflt_eventq(),slow_advertising,0)==0);
 configASSERT(ble_npl_callout_init(&param_timer,nimble_port_get_dflt_eventq(),request_parameters,0)==0);
 STAT_SET(mtu,23);nimble_port_freertos_init(host);
}
