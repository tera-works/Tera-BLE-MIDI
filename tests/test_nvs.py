"""Compile actual config_nvs.c against a deterministic NVS/RTOS mock."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
scratch = root/'tests'/'.policy-tests'
scratch.mkdir(exist_ok=True)
source = (root/'main/config_nvs.c').read_text(encoding='utf-8-sig')
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
mock = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include "name_command.h"
#define CONFIG_TERA_DEBUG 0
#define CONFIG_TERA_NAME "ESP MIDI"
#define SOURCE_DEFAULT 0
#define SOURCE_NVS 1
#define BAUD_DEFAULT 31250
#define ESP_OK 0
#define ESP_ERR_NVS_NO_FREE_PAGES 1
#define ESP_ERR_NVS_NEW_VERSION_FOUND 2
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define configASSERT(x) assert(x)
#define STAT_GET(x) connected
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef int StaticQueue_t;
typedef int StaticTask_t;
typedef uint32_t StackType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
static bool connected,disconnect_on_wait;
static uint32_t ticks,db_baud,db_version;
static char db_name[80];
static unsigned commits,sets,queue_count,queue_size;
static uint8_t queue_data[8][80];
static jmp_buf finish;
static int64_t esp_timer_get_time(void){return (int64_t)ticks*1000;}
static int nvs_flash_init(void){return 0;}
static int nvs_flash_erase(void){db_version=0;return 0;}
static int nvs_open(const char *ns,int mode,nvs_handle_t *h){(void)ns;(void)mode;*h=1;return 0;}
static void nvs_close(int h){(void)h;}
static int nvs_get_u32(int h,const char *key,uint32_t *v) {
 (void)h;*v=!strcmp(key,"version")?db_version:db_baud;return *v?0:3;
}
static int nvs_get_str(int h,const char *key,char *s,size_t *n) {
 (void)h;(void)key;if(!db_name[0]||strlen(db_name)+1>*n)return 3;
 strcpy(s,db_name);*n=strlen(s)+1;return 0;
}
static int nvs_set_u32(int h,const char *key,uint32_t v) {
 (void)h;sets++;if(!strcmp(key,"version"))db_version=v;else db_baud=v;return 0;
}
static int nvs_set_str(int h,const char *key,const char *s){(void)h;(void)key;sets++;strcpy(db_name,s);return 0;}
static int nvs_commit(int h){(void)h;commits++;assert(ticks>=2000);return 0;}
static bool baud_supported(uint32_t b){return b==31250||b==38400||b==57600||b==115200||b==230400;}
static void *xQueueCreateStatic(unsigned n,unsigned size,void *mem,void *obj) {
 (void)n;(void)mem;(void)obj;queue_count=0;queue_size=size;return queue_data;
}
static int xQueueSend(void *q,const void *r,unsigned wait) {
 (void)q;(void)wait;if(queue_count==8)return 0;memcpy(queue_data[queue_count++],r,queue_size);return 1;
}
static int xQueueReceive(void *q,void *r,unsigned wait) {
 (void)q;if(!queue_count){if(wait==portMAX_DELAY)longjmp(finish,1);return 0;}
 memcpy(r,queue_data[0],queue_size);queue_count--;
 memmove(queue_data[0],queue_data[1],queue_count*sizeof(queue_data[0]));return 1;
}
static unsigned ulTaskNotifyTake(int clear,unsigned wait) {
 (void)clear;
 if(wait==portMAX_DELAY){if(disconnect_on_wait){connected=false;disconnect_on_wait=false;return 1;}longjmp(finish,2);}
 ticks+=wait;return 0;
}
static void xTaskNotifyGive(void *t){(void)t;}
static void *xTaskCreateStatic(void (*fn)(void *),const char *name,unsigned sz,void *arg,unsigned prio,void *stk,void *obj) {
 (void)fn;(void)name;(void)sz;(void)arg;(void)prio;(void)stk;(void)obj;return (void *)1;
}
'''
tests = r'''
static void run(void){if(setjmp(finish)==0)worker(0);}
static void reset(void){ticks=0;connected=false;disconnect_on_wait=false;commits=sets=0;}
int main(void) {
 uint32_t source;reset();db_version=db_baud=0;db_name[0]=0;
 assert(config_load(&source)==31250&&source==SOURCE_DEFAULT&&!strcmp(config_name(),"ESP MIDI"));
 config_name_later("Stage KB");run();assert(commits==1&&!strcmp(db_name,"Stage KB"));
 assert(!strcmp(config_name(),"ESP MIDI")); /* active name remains stable */
 reset();assert(config_load(&source)==31250&&!strcmp(config_name(),"Stage KB"));
 config_name_later("Stage KB");run();assert(!commits&&!sets);
 config_name_later("bad\nname");run();assert(!commits);
 reset();db_version=1;db_baud=115200;strcpy(db_name,"Saved KB");
 assert(config_load(&source)==115200&&source==SOURCE_NVS&&!strcmp(config_name(),"Saved KB"));
 config_save_later(230400);config_name_later("Newest KB");config_name_later("Last KB");
 connected=true;disconnect_on_wait=true;run();
 assert(commits==2&&db_baud==230400&&!strcmp(db_name,"Last KB"));
 reset();db_version=99;db_baud=230400;strcpy(db_name,"Stale");
 assert(config_load(&source)==31250&&source==SOURCE_DEFAULT&&!strcmp(config_name(),"ESP MIDI"));
 reset();db_version=1;db_baud=38400;strcpy(db_name,"bad\nname");
 assert(config_load(&source)==38400&&!strcmp(config_name(),"ESP MIDI"));
 reset();db_version=1;db_baud=31250;memset(db_name,'X',50);db_name[50]=0;
 assert(config_load(&source)==31250&&!strcmp(config_name(),"ESP MIDI"));
 config_name_later("Live Rename");connected=true;run();assert(commits==1&&!strcmp(db_name,"Live Rename"));
 puts("PASS: default, reboot persistence, unchanged no-write, invalid/version fallback, merged baud/name, connected idle save");
 return 0;
}
'''
c = scratch/'nvs_test.c'
c.write_text(mock+'\n'+source+'\n'+tests, encoding='utf-8')
exe = scratch/('nvs_test.exe' if os.name == 'nt' else 'nvs_test')
subprocess.run([os.environ.get('CC','gcc'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'main'),str(c),str(root/'main/name_command.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
