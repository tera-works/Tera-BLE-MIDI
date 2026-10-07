from pathlib import Path
import os
root=Path(__file__).resolve().parents[1]
scratch=root/"tests"/".policy-tests"
scratch.mkdir(parents=True,exist_ok=True)
import subprocess,re
s=(root/'main/power_manager.c').read_text()
s=re.sub(r'^#include.*\n','',s,flags=re.M)
t=r'''
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define CONFIG_TERA_DEBUG 0
#define ESP_PM_CPU_FREQ_MAX 1
#define ESP_ERROR_CHECK(x) assert((x)==0)
typedef int esp_err_t;typedef void *esp_pm_lock_handle_t;
typedef struct {int max_freq_mhz,min_freq_mhz;bool light_sleep_enable;} esp_pm_config_t;
static esp_pm_config_t configured;static int enabled,disabled,locks,sleep_rc;
static int esp_pm_configure(const esp_pm_config_t *c){configured=*c;return 0;}
static int esp_pm_lock_create(int t,int n,const char *name,void **h){(void)t;(void)n;(void)name;*h=(void*)1;return 0;}
static void esp_pm_lock_acquire(void *h){assert(h==(void*)1);locks++;}
static void esp_pm_lock_release(void *h){assert(h==(void*)1);locks--;}
static int esp_bt_sleep_enable(void){enabled++;return sleep_rc;}
static int esp_bt_sleep_disable(void){disabled++;return sleep_rc;}
''' + s + r'''
int main(void){power_init();assert(configured.max_freq_mhz==240&&configured.min_freq_mhz==40&&!configured.light_sleep_enable);
 power_connected(true);assert(disabled==1&&enabled==0);
 power_connected(false);assert(enabled==1&&disabled==1);
 power_enter();power_enter();assert(locks==2);power_exit();power_exit();assert(locks==0);
 sleep_rc=42;power_connected(true);power_connected(false);assert(disabled==2&&enabled==2);
 puts("PASS: modem awake while connected, sleep restored for advertising, DFS/CPU locks, unsupported-state nonfatal");}
'''
Path(str(scratch/'power_policy_test.c')).write_text(t)
subprocess.run([str(Path(os.environ.get('CC','gcc'))),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(scratch/'power_policy_test.c'),'-o',str(scratch/'power_policy_test.exe')],check=True)
r=subprocess.run([str(scratch/'power_policy_test.exe')],check=True,capture_output=True,text=True)
(scratch/'power-policy-test.log').write_text(r.stdout);print(r.stdout)
