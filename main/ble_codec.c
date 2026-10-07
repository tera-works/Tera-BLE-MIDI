#include "ble_codec.h"
#include <string.h>
void ble_encoder_init(ble_encoder_t *e,ble_packet_fn fn,void *ctx) {
 memset(e,0,sizeof(*e));e->limit=20;e->sink=fn;e->ctx=ctx;
}
bool ble_encode(ble_encoder_t *e,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 uint8_t pkt[BLE_PACKET_MAX];size_t k=1;
 if(!d||!n||e->limit<5||e->limit>sizeof(pkt)){e->errors++;return false;}
 pkt[0]=0x80|((ts>>7)&0x3f);
 if(kind!=MIDI_SYSEX) {
  if(n>3||n+2>e->limit){e->errors++;return false;}
  pkt[k++]=0x80|(ts&0x7f);memcpy(pkt+k,d,n);k+=n;
  return e->sink(e->ctx,pkt,k);
 }
 for(size_t i=0;i<n;i++) {
  uint8_t b=d[i];
  if(b==0xf0) {e->sysex=true;pkt[k++]=0x80|(ts&0x7f);}
  else if(!e->sysex){e->errors++;return false;}
  if(b==0xf7) {
   if(k+2>e->limit) {if(!e->sink(e->ctx,pkt,k)){e->sysex=false;return false;} k=1;}
   pkt[k++]=0x80|(ts&0x7f);e->sysex=false;
  } else if(b>=0x80&&b!=0xf0){e->errors++;e->sysex=false;return false;}
  if(k==e->limit) {if(!e->sink(e->ctx,pkt,k)){e->sysex=false;return false;} k=1;}
  pkt[k++]=b;
 }
 if(k>1&&!e->sink(e->ctx,pkt,k)){e->sysex=false;return false;}
 return true;
}
void ble_decoder_init(ble_decoder_t *d,midi_sink_fn fn,void *ctx) {memset(d,0,sizeof(*d));midi_init(&d->midi,fn,ctx);}
static bool fail(ble_decoder_t *d) {
 d->errors++;
 if(d->midi.in_sysex){d->midi.sysex_aborted=true;midi_feed(&d->midi,0xf7,d->last_timestamp);}
 midi_reset(&d->midi);return false;
}
bool ble_decode(ble_decoder_t *d,const uint8_t *pkt,size_t n) {
 if(!pkt||n<2||(pkt[0]&0xc0)!=0x80)return fail(d);
 /* Running status never crosses BLE packet boundaries. SysEx does. */
 if(d->midi.need)return fail(d);
 d->midi.running=d->midi.status=0;
 if(!(pkt[1]&0x80)&&!d->midi.in_sysex)return fail(d);
 uint16_t hi=(pkt[0]&0x3f)<<7,ts=(pkt[1]&0x80)?hi:d->last_timestamp;
 uint8_t last=0,running=0;bool have=false,got=false,after_system=false;
 uint32_t bad=d->midi.invalid;
 for(size_t i=1;i<n;i++) {
  uint8_t b=pkt[i];
  if(b&0x80) {
   if(!got) {
    midi_flush(&d->midi);uint8_t lo=b&0x7f;
    if(have&&lo<last)hi=(hi+128)&8191;
    last=lo;have=true;ts=hi|lo;d->last_timestamp=ts;got=true;continue;
   }
   got=false;
   if(b<0xf0){running=b;after_system=false;}else after_system=true;
   midi_feed(&d->midi,b,ts);
   /* BLE running status survives System Common, unlike the UART stream. */
   if(!d->midi.need&&!d->midi.in_sysex)d->midi.running=running;
  } else {
   if(!d->midi.in_sysex&&!d->midi.need&&after_system&&!got)return fail(d);
   got=false;
   if(!d->midi.need&&!d->midi.in_sysex){d->midi.running=running;after_system=false;}
   midi_feed(&d->midi,b,ts);
   if(!d->midi.need&&!d->midi.in_sysex)d->midi.running=running;
  }
 }
 midi_flush(&d->midi);
 if(got||d->midi.need||d->midi.invalid!=bad)return fail(d);
 d->midi.running=d->midi.status=0;return true;
}
