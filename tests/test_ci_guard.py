from pathlib import Path
import os
root=Path(__file__).resolve().parents[1]
scratch=root/"tests"/".policy-tests"
scratch.mkdir(parents=True,exist_ok=True)
import re,subprocess
s=(root/'main/ble_midi.c').read_text()
fn=s[s.index('static void request_parameters('):s.index('static void update_connection(')]
test=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define BLE_HS_CONN_HANDLE_NONE 65535
struct ble_npl_event {int unused;};
struct ble_gap_conn_desc {unsigned conn_itvl,conn_latency;};
struct ble_gap_upd_params {unsigned itvl_min,itvl_max,latency,supervision_timeout;};
static uint16_t connection=1;
static unsigned calls,find_error;
static struct ble_gap_conn_desc actual;
static struct ble_gap_upd_params last;
static int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *d){(void)h;*d=actual;return find_error;}
static int ble_gap_update_params(uint16_t h,struct ble_gap_upd_params *p){(void)h;calls++;last=*p;return 0;}
''' + fn + r'''
int main(void){
 struct ble_npl_event delayed={0};
 actual=(struct ble_gap_conn_desc){6,0};request_parameters(0);
 assert(calls==1&&last.itvl_min==6&&last.itvl_max==6&&last.latency==0&&last.supervision_timeout==100);
 request_parameters(&delayed);assert(calls==1);
 actual=(struct ble_gap_conn_desc){12,0};request_parameters(&delayed);
 assert(calls==2&&last.itvl_min==6&&last.itvl_max==12);
 actual=(struct ble_gap_conn_desc){6,1};request_parameters(&delayed);assert(calls==3&&last.latency==0);
 actual=(struct ble_gap_conn_desc){6,0};request_parameters(&delayed);assert(calls==3);
 connection=BLE_HS_CONN_HANDLE_NONE;request_parameters(0);request_parameters(&delayed);assert(calls==3);
 puts("PASS: exact request, no downgrade at 7.5ms/latency0, fallback for slower interval/nonzero latency, disconnected no-op");
}
'''
Path(str(scratch/'ci_guard_test.c')).write_text(test)
gcc=Path(os.environ.get('CC','gcc'))
subprocess.run([str(gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(scratch/'ci_guard_test.c'),'-o',str(scratch/'ci_guard_test.exe')],check=True)
r=subprocess.run([str(scratch/'ci_guard_test.exe')],check=True,capture_output=True,text=True)
(scratch/'ci-guard-test.log').write_text(r.stdout);print(r.stdout)
