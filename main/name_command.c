#include "name_command.h"
#include <string.h>
enum { IDLE, PREFIX, PASS, COMMAND, DROP };
static const uint8_t header[]={0xf0,0x7d,0x45,0x53,0x50,0x01,0x01};
bool name_valid(const char *name) {
 size_t n=0;bool visible=false;
 while(name[n]) {
  unsigned char b=(unsigned char)name[n++];
  if(n>MIDI_NAME_MAX||b<0x20||b>0x7e)return false;
  if(b!=' ')visible=true;
 }
 return n&&visible;
}
void name_filter_init(name_filter_t *f,midi_sink_fn sink,void *ctx,name_set_fn set) {
 memset(f,0,sizeof(*f));f->sink=sink;f->ctx=ctx;f->set=set;
}
void name_filter_reset(name_filter_t *f){f->mode=IDLE;f->used=f->length=0;}
bool name_filter_abort(name_filter_t *f) {
 bool held=f->mode==PREFIX||f->mode==COMMAND||f->mode==DROP;
 name_filter_reset(f);return held;
}
static void output(name_filter_t *f,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 if(n&&f->sink)f->sink(f->ctx,d,n,ts,kind);
}
void name_filter_feed(name_filter_t *f,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 if(kind!=MIDI_SYSEX) {
  if(kind!=MIDI_REALTIME&&f->mode!=IDLE)name_filter_reset(f);
  output(f,d,n,ts,kind);return;
 }
 size_t i=0;
 while(i<n) {
  uint8_t b=d[i];
  if(b==0xf0){name_filter_reset(f);f->mode=PREFIX;f->timestamp=ts;}
  if(f->mode==IDLE||f->mode==PASS) {
   /* Forward ordinary SysEx by chunks rather than one byte at a time. */
   output(f,d+i,n-i,ts,kind);
   if(d[n-1]==0xf7)name_filter_reset(f);
   return;
  }
  if(f->mode==PREFIX) {
   f->prefix[f->used++]=b;
   if(b!=header[f->used-1]) {
    output(f,f->prefix,f->used,f->timestamp,kind);f->used=0;f->mode=PASS;
    if(b==0xf7)name_filter_reset(f);
   } else if(f->used==sizeof(header)){f->mode=COMMAND;f->length=0;}
  } else if(b==0xf7) {
   if(f->mode==COMMAND) {
    while(f->length&&f->name[f->length-1]==' ')f->length--;
    f->name[f->length]=0;
    if(name_valid(f->name)&&f->set)f->set(f->name);
   }
   name_filter_reset(f);
  } else if(f->mode==COMMAND) {
   if(b<0x20||b>0x7e||f->length==MIDI_NAME_MAX)f->mode=DROP;
   else f->name[f->length++]=(char)b;
  }
  i++;
 }
}
