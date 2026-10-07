#include "midi_parser.h"
#include <string.h>
static void emit(midi_parser_t *p,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 if(p->sink) p->sink(p->ctx,d,n,ts,kind);
}
void midi_init(midi_parser_t *p,midi_sink_fn fn,void *ctx) {memset(p,0,sizeof(*p));p->sink=fn;p->ctx=ctx;}
void midi_flush(midi_parser_t *p) {
 if(p->sx_used) {emit(p,p->sx,p->sx_used,p->sx_timestamp,MIDI_SYSEX);p->sx_used=0;}
}
void midi_reset(midi_parser_t *p) {
 p->running=p->status=p->need=p->used=p->sx_used=0;p->in_sysex=p->sysex_aborted=false;
}
static void sysex_byte(midi_parser_t *p,uint8_t b,uint16_t ts) {
 if(!p->sx_used)p->sx_timestamp=ts;
 p->sx[p->sx_used++]=b;
 if(p->sx_used==MIDI_CHUNK)midi_flush(p);
}
void midi_feed(midi_parser_t *p,uint8_t b,uint16_t ts) {
 p->bytes++;
 if(b>=0xf8) {
  if(b==0xf9||b==0xfd){p->invalid++;return;}
  midi_flush(p);p->valid++;emit(p,&b,1,ts,MIDI_REALTIME);return;
 }
 if(p->in_sysex) {
  if(b<0x80){sysex_byte(p,b,ts);return;}
  if(b==0xf7){sysex_byte(p,b,ts);midi_flush(p);p->in_sysex=false;p->valid++;p->structured++;return;}
  /* Close already streamed SysEx before resynchronising on a new status. */
  p->invalid++;p->sysex_aborted=true;sysex_byte(p,0xf7,ts);midi_flush(p);p->in_sysex=false;p->sysex_aborted=false;
 }
 if(b&0x80) {
  if(p->used||p->need)p->invalid++;
  p->used=p->need=0;p->status=b;p->timestamp=ts;
  if(b<0xf0) {p->running=b;p->need=((b&0xe0)==0xc0)?1:2;return;}
  p->running=0;
  if(b==0xf0){p->status=0;p->in_sysex=true;sysex_byte(p,b,ts);return;}
  if(b==0xf6){p->valid++;p->structured++;emit(p,&b,1,ts,MIDI_SHORT);p->status=0;return;}
  if(b==0xf1||b==0xf3){p->need=1;return;}
  if(b==0xf2){p->need=2;return;}
  p->invalid++;p->status=0;return;
 }
 if(!p->need) {
  if(!p->running){p->invalid++;return;}
  p->status=p->running;p->need=((p->status&0xe0)==0xc0)?1:2;p->timestamp=ts;
 }
 p->data[p->used++]=b;
 if(p->used==p->need) {
  uint8_t d[3]={p->status,p->data[0],p->data[1]};
  p->valid++;p->structured++;emit(p,d,p->need+1,p->timestamp,MIDI_SHORT);
  p->used=p->need=0;
  if(p->status>=0xf0)p->status=0;
 }
}
