#include "name_command.h"
#include "ble_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static name_filter_t filter;
static midi_parser_t parser;
static unsigned changes;static char saved[30];static uint8_t out[4096];static size_t used;
static void sink(void *ctx,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 (void)ctx;(void)ts;(void)kind;assert(used+n<=sizeof(out));memcpy(out+used,d,n);used+=n;
}
static void rename_name(const char *s){strcpy(saved,s);changes++;}
static void parsed(void *ctx,const uint8_t *d,size_t n,uint16_t ts,uint8_t kind) {
 midi_parser_t *p=ctx;
 if(p->sysex_aborted&&name_filter_abort(&filter))return;
 name_filter_feed(&filter,d,n,ts,kind);
}
static void init(void) {
 changes=0;used=0;saved[0]=0;
 name_filter_init(&filter,sink,0,rename_name);midi_init(&parser,parsed,&parser);
}
static void feed(const uint8_t *d,size_t n) {
 for(size_t i=0;i<n;i++)midi_feed(&parser,d[i],123);
 midi_flush(&parser);
}
static bool packet(void *ctx,const uint8_t *d,size_t n){return ble_decode(ctx,d,n);}
int main(void) {
 const uint8_t cmd[]={0xf0,0x7d,0x45,0x53,0x50,1,1,'S','t','a','g','e',' ','K','B',0xf7};
 for(size_t split=1;split<sizeof(cmd);split++) {
  init();feed(cmd,split);assert(!changes);feed(cmd+split,sizeof(cmd)-split);
  assert(changes==1&&strcmp(saved,"Stage KB")==0&&used==0);
 }
 init();for(size_t i=0;i<sizeof(cmd);i++){feed(cmd+i,1);const uint8_t rt=0xf8;feed(&rt,1);}
 assert(changes==1&&used==sizeof(cmd));for(size_t i=0;i<used;i++)assert(out[i]==0xf8);
 uint8_t bad[80];memcpy(bad,cmd,sizeof(cmd));bad[8]=0x01;
 init();feed(bad,sizeof(cmd));assert(!changes&&used==0);
 init();feed(cmd,10);const uint8_t note[]={0x90,60,100};feed(note,3);
 assert(!changes&&used==3&&memcmp(out,note,3)==0);
 init();feed(cmd,10);name_filter_reset(&filter);midi_reset(&parser);feed(cmd+10,sizeof(cmd)-10);assert(!changes);
 memcpy(bad,cmd,7);memset(bad+7,'A',30);bad[37]=0xf7;
 init();feed(bad,38);assert(!changes&&used==0);
 memset(bad+7,'A',29);bad[36]=0xf7;init();feed(bad,37);assert(changes==1&&strlen(saved)==29);
 memcpy(bad,cmd,7);bad[7]=0xf7;init();feed(bad,8);assert(!changes);
 memcpy(bad,cmd,7);memset(bad+7,' ',10);bad[17]=0xf7;init();feed(bad,18);assert(!changes);
 const uint8_t normal[]={0xf0,0x43,0x7d,1,2,3,0xf7};
 for(size_t split=1;split<sizeof(normal);split++) {
  init();feed(normal,split);feed(normal+split,sizeof(normal)-split);
  assert(!changes&&used==sizeof(normal)&&memcmp(out,normal,used)==0);
 }
 memcpy(bad,cmd,sizeof(cmd));bad[6]=2;init();feed(bad,sizeof(cmd));assert(!changes&&used==sizeof(cmd)&&memcmp(out,bad,used)==0);
 init();bad[0]=0xf0;bad[1]=0x43;memset(bad+2,0x12,77);bad[79]=0xf7;
 for(unsigned repeat=0;repeat<20;repeat++)feed(bad,sizeof(bad));
 assert(used==1600&&!changes);
 /* Actual encoder -> decoder at MTU23, command across BLE packet boundaries. */
 init();ble_decoder_t dec;ble_decoder_init(&dec,parsed,&dec.midi);
 ble_encoder_t enc;ble_encoder_init(&enc,packet,&dec);enc.limit=20;
 assert(ble_encode(&enc,cmd,10,8190,MIDI_SYSEX));assert(!changes);
 assert(ble_encode(&enc,cmd+10,sizeof(cmd)-10,2,MIDI_SYSEX));assert(changes==1&&!strcmp(saved,"Stage KB")&&used==0);
 init();ble_decoder_init(&dec,parsed,&dec.midi);ble_encoder_init(&enc,packet,&dec);enc.limit=20;
 assert(ble_encode(&enc,cmd,10,123,MIDI_SYSEX));assert(!ble_decode(&dec,0,0));assert(!changes);
 assert(name_valid("ESP MIDI")&&!name_valid("")&&!name_valid("  ")&&!name_valid("bad\nname"));
 puts("PASS: name commands, every UART split, realtime, malformed abort, bounds, passthrough, BLE fragmentation");return 0;
}
