#include "config_nvs.h"
#include "baud_detect.h"
#include "runtime.h"
#include "name_command.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
enum { SAVE_BAUD=1,SAVE_NAME=2 };
typedef struct {uint32_t baud;uint8_t fields;char name[MIDI_NAME_MAX+1];} request_t;
static QueueHandle_t queue;
static StaticQueue_t qobj;
static uint8_t qmem[8*sizeof(request_t)];
static StaticTask_t taskobj;static StackType_t stack[3072/sizeof(StackType_t)];
static TaskHandle_t worker_task;
static uint32_t saved,last_activity;
static bool available;
/* Immutable boot-time name: rename activates on reboot. */
static char boot_name[MIDI_NAME_MAX+1],saved_name[MIDI_NAME_MAX+1];
const char *config_name(void){return boot_name;}
void config_activity(void){__atomic_store_n(&last_activity,(uint32_t)(esp_timer_get_time()/1000),__ATOMIC_RELAXED);}
static void merge(request_t *pending,const request_t *r) {
 if(r->fields&SAVE_BAUD)pending->baud=r->baud;
 if(r->fields&SAVE_NAME)memcpy(pending->name,r->name,sizeof(pending->name));
 pending->fields|=r->fields;
}
static void worker(void *arg) {
 (void)arg;request_t pending={0},r;
 for(;;) {
  if(!pending.fields)xQueueReceive(queue,&pending,portMAX_DELAY);
  while(xQueueReceive(queue,&r,0)==pdTRUE)merge(&pending,&r);
  if((pending.fields&SAVE_BAUD)&&pending.baud==__atomic_load_n(&saved,__ATOMIC_RELAXED))pending.fields&=~SAVE_BAUD;
  if((pending.fields&SAVE_NAME)&&strcmp(pending.name,saved_name)==0)pending.fields&=~SAVE_NAME;
  if(!pending.fields)continue;
  uint32_t quiet=(uint32_t)(esp_timer_get_time()/1000)-__atomic_load_n(&last_activity,__ATOMIC_RELAXED);
  if(quiet<2000){ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(2000-quiet)+1);continue;}
  uint8_t fields=pending.fields;
  /* Automatic baud saves require disconnection. Explicit rename can save
     while connected after two seconds of MIDI silence. */
  if(STAT_GET(connected))fields&=~SAVE_BAUD;
  if(!fields){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;}
  nvs_handle_t h;esp_err_t e=nvs_open("tera_uart",NVS_READWRITE,&h);
  if(e==ESP_OK) {
   e=nvs_set_u32(h,"version",1);
   if(e==ESP_OK&&(fields&SAVE_BAUD))e=nvs_set_u32(h,"baud",pending.baud);
   if(e==ESP_OK&&(fields&SAVE_NAME))e=nvs_set_str(h,"name",pending.name);
   if(e==ESP_OK)e=nvs_commit(h);
   nvs_close(h);
  }
  if(e==ESP_OK) {
   if(fields&SAVE_BAUD)__atomic_store_n(&saved,pending.baud,__ATOMIC_RELAXED);
   if(fields&SAVE_NAME) {
    memcpy(saved_name,pending.name,sizeof(saved_name));
#if CONFIG_TERA_DEBUG
    ESP_LOGI("TERA","name saved: %s (active on reboot)",saved_name);
#endif
   }
  }
#if CONFIG_TERA_DEBUG
  else ESP_LOGW("TERA","config save failed: %s",esp_err_to_name(e));
#endif
  pending.fields&=~fields;
 }
}
uint32_t config_load(uint32_t *source) {
 esp_err_t e=nvs_flash_init();
 if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND) {
  e=nvs_flash_erase();if(e==ESP_OK)e=nvs_flash_init();
 }
 available=e==ESP_OK;uint32_t b=BAUD_DEFAULT,v=0;*source=SOURCE_DEFAULT;
 configASSERT(name_valid(CONFIG_TERA_NAME));strcpy(boot_name,CONFIG_TERA_NAME);
 nvs_handle_t h;
 if(available&&nvs_open("tera_uart",NVS_READONLY,&h)==ESP_OK) {
  if(nvs_get_u32(h,"version",&v)==ESP_OK&&v==1) {
   if(nvs_get_u32(h,"baud",&b)==ESP_OK&&baud_supported(b))*source=SOURCE_NVS;
   else b=BAUD_DEFAULT;
   char name[MIDI_NAME_MAX+1];size_t len=sizeof(name);
   if(nvs_get_str(h,"name",name,&len)==ESP_OK&&name_valid(name))strcpy(boot_name,name);
  }
  nvs_close(h);
 }
 strcpy(saved_name,boot_name);saved=(*source==SOURCE_NVS)?b:0;
 queue=xQueueCreateStatic(8,sizeof(request_t),qmem,&qobj);configASSERT(queue);
 config_activity();
 if(available){worker_task=xTaskCreateStatic(worker,"config_nvs",sizeof(stack),0,1,stack,&taskobj);configASSERT(worker_task);}
 return b;
}
static void enqueue(const request_t *r) {
 if(available&&xQueueSend(queue,r,0)==pdTRUE)xTaskNotifyGive(worker_task);
}
void config_save_later(uint32_t b) {
 if(b!=__atomic_load_n(&saved,__ATOMIC_RELAXED)) {request_t r={.baud=b,.fields=SAVE_BAUD};enqueue(&r);}
}
void config_name_later(const char *name) {
 if(!name_valid(name))return;
 request_t r={.fields=SAVE_NAME};strcpy(r.name,name);enqueue(&r);
}
void config_link_changed(void){if(worker_task)xTaskNotifyGive(worker_task);}
