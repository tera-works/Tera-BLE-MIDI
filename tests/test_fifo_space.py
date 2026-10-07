from pathlib import Path
import os
root=Path(__file__).resolve().parents[1]
scratch=root/"tests"/".policy-tests"
scratch.mkdir(parents=True,exist_ok=True)
import subprocess
s=(root/'main/uart_midi.c').read_text()
fn=s[s.index('static void IRAM_ATTR fill_tx('):s.index('static void IRAM_ATTR interrupt(')]
stub=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define IRAM_ATTR
#define UART_TX_RING_SIZE 4096
#define UART_INTR_TXFIFO_EMPTY 1
static void *hw;
static uint8_t tx[UART_TX_RING_SIZE],written[256];
static unsigned th,tt,space,wcount;
static bool tx_eox_pending,irq;
static unsigned uart_ll_get_txfifo_len(void *h){(void)h;return space;}
static void uart_ll_write_txfifo(void *h,const uint8_t *d,unsigned n){(void)h;assert(n<=space);for(unsigned i=0;i<n;i++)written[wcount++]=d[i];space-=n;}
static void uart_ll_ena_intr_mask(void *h,unsigned m){(void)h;(void)m;irq=true;}
static void uart_ll_disable_intr_mask(void *h,unsigned m){(void)h;(void)m;irq=false;}
''' + fn+r'''
static void reset(unsigned free){th=tt=wcount=0;space=free;tx_eox_pending=false;irq=false;}
int main(void){
 reset(128);th=3;fill_tx();assert(wcount==3&&tt==th&&!irq&&space==125);
 reset(0);th=3;fill_tx();assert(wcount==0&&tt==0&&irq);space=8;fill_tx();assert(wcount==3&&tt==th&&!irq);
 reset(2);th=3;fill_tx();assert(wcount==2&&tt==2&&irq);space=128;fill_tx();assert(wcount==3&&tt==th&&!irq);
 reset(128);tt=4095;th=2;fill_tx();assert(wcount==3&&tt==2&&!irq);
 reset(1);tx_eox_pending=true;fill_tx();assert(wcount==1&&written[0]==0xf7&&!tx_eox_pending&&!irq);
 reset(0);tx_eox_pending=true;fill_tx();assert(wcount==0&&tx_eox_pending&&irq);space=2;fill_tx();assert(wcount==1&&!tx_eox_pending&&!irq);
 puts("PASS: empty/full/partial FIFO, refill interrupt, wrapped ring, SysEx recovery EOX; writable-space semantics");
}
'''
Path(str(scratch/'fifo_space_test.c')).write_text(stub)
subprocess.run([str(Path(os.environ.get('CC','gcc'))),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(scratch/'fifo_space_test.c'),'-o',str(scratch/'fifo_space_test.exe')],check=True)
r=subprocess.run([str(scratch/'fifo_space_test.exe')],check=True,capture_output=True,text=True)
(scratch/'fifo-space-test.log').write_text(r.stdout);print(r.stdout)
